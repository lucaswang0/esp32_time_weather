# pipeline.py
import socket
import threading
import queue
import time
from PIL import Image, ImageDraw
import struct
import numpy as np
from collections import deque
import mss
from numba import jit, types

from window_capture import WindowScreenshotter

@jit(nopython=True)
def rgb_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

class _DummyMetric:
    def labels(self, **kwargs):
        return self
    def observe(self, value):
        pass
    def inc(self):
        pass
    def set(self, value):
        pass

class _DummyMetricsDict:
    def __getitem__(self, key):
        return _DummyMetric()

class StreamPipeline:
    """
    Manages a single streaming pipeline: port listening, frame generation,
    processing and sending to client.
    """
    def __init__(self, device_config, global_server_stop_event, prometheus_metrics_objects=None):
        self.config = device_config
        self.name = self.config.get('name', 'UnnamedPipeline')
        self.port = self.config['esp32_port']
        self.global_server_stop_event = global_server_stop_event
        self.metrics = prometheus_metrics_objects if prometheus_metrics_objects else _DummyMetricsDict()

        self.frames_queue = queue.Queue(maxsize=self.config.get('frames_queue_max_size', 5))
        self.pipeline_internal_stop_event = threading.Event()

        self.server_socket = None
        self.client_connection = None
        self.manager_thread = None

        self._generator_thread = None
        self._consumer_thread = None
        self._generator_instance = None
        self._sct_instance_local_to_generator_thread = None

        self._prev_processed_image = None
        self._current_dynamic_threshold = self.config.get('min_dirty_rect_threshold', 10)
        self._frame_processing_times_history = deque(maxlen=self.config.get('fps_history_size', 10))

        self._log(f"Pipeline instance created for port {self.port}.")

    def _log(self, message, level="INFO"):
        """Logs messages with pipeline name."""
        print(f"[{level}][{self.name}] {message}")

    def _initialize_generator_instance(self):
        """Initializes or reinitializes the specific image generator."""
        self._log("Initializing generator settings...")
        source_mode = self.config['image_source_mode']
        generator_canvas_resolution = (self.config['target_width'], self.config['target_height'])

        if self._generator_instance and hasattr(self._generator_instance, 'stop'):
            try: self._generator_instance.stop()
            except Exception as e: self._log(f"Error stopping previous generator: {e}", "WARN")
        self._generator_instance = None

        try:
            if source_mode == "WINDOW_CAPTURE":
                self._generator_instance = WindowScreenshotter(
                    self.config.get('window_title', None),
                    self.config.get('crop_alignment', 'left'),
                    resolution=(self.config['target_width'], self.config['target_height'])
                )
            elif source_mode == "SCREEN_CAPTURE": 
                capture_region = self.config.get('capture_region')
                if not capture_region:
                    self._log("capture_region not specified for SCREEN_CAPTURE mode!", "ERROR")
                    return False
            else:
                self._log(f"Unsupported image_source_mode: {source_mode}", "ERROR")
                return False
            self._log(f"Settings for generator '{source_mode}' prepared successfully.")
            return True
        except Exception as e:
            self._log(f"CRITICAL ERROR preparing generator for '{source_mode}': {e}", "ERROR")
            import traceback
            traceback.print_exc()
            return False

    def _apply_gamma_and_white_balance(self, img: Image.Image) -> Image.Image:
        """Applies gamma correction and white balance to the image."""
        if img.mode != 'RGB': img = img.convert('RGB')
        img_array = np.array(img, dtype=np.float32) / 255.0
        
        gamma = self.config.get('gamma', 1.0)
        img_corrected = np.power(img_array, gamma)
        
        wb_scale_config = self.config.get('wb_scale', (1.0, 1.0, 1.0))
        if not (isinstance(wb_scale_config, (list, tuple)) and len(wb_scale_config) == 3):
            self._log(f"Invalid wb_scale format: {wb_scale_config}. Using (1.0, 1.0, 1.0).", "WARN")
            wb_scale_config = (1.0, 1.0, 1.0)
            
        scale_np = np.array(wb_scale_config).reshape(1, 1, 3)
        img_balanced = img_corrected * scale_np
        
        img_final_np = np.clip(img_balanced * 255.0, 0, 255).astype(np.uint8)

        return Image.fromarray(img_final_np, 'RGB')

    def _image_to_rgb565_bytes(self, img: Image.Image) -> bytes:
        """Converts PIL Image to RGB565 bytes after applying corrections."""
        processing_start_time = time.monotonic()
        try:
            processed_img = self._apply_gamma_and_white_balance(img)
        except Exception as e:
            self._log(f"Error applying gamma/white balance: {e}. Using original image.", "WARN")
            processed_img = img.convert('RGB') if img.mode != 'RGB' else img
        self.metrics['frame_processing_time'].labels(stage='color_correction', pipeline_name=self.name).observe(time.monotonic() - processing_start_time)

        conversion_start_time = time.monotonic()
        if processed_img.mode != 'RGB':
            processed_img = processed_img.convert('RGB')
            
        pixels = processed_img.load()
        width, height = processed_img.size
        byte_data = bytearray(width * height * 2)
        idx = 0
        for y_coord in range(height):
            for x_coord in range(width):
                try:
                    pixel_val = pixels[x_coord, y_coord]
                    if isinstance(pixel_val, int):
                         r, g, b = pixel_val, pixel_val, pixel_val
                    else:
                         r, g, b = pixel_val[:3]
                except (TypeError, IndexError) as e:
                    self._log(f"Invalid pixel ({pixels[x_coord, y_coord]}, mode: {processed_img.mode}) during RGB565 conversion: {e}. Skipping frame.", "ERROR")
                    return b'' 
                rgb565_val = rgb_to_rgb565(r, g, b)
                struct.pack_into('<H', byte_data, idx, rgb565_val)
                idx += 2
        self.metrics['frame_processing_time'].labels(stage='rgb565_conversion', pipeline_name=self.name).observe(time.monotonic() - conversion_start_time)
        return bytes(byte_data)

    def _find_dirty_rects(self, img_prev: Image.Image | None, img_curr: Image.Image):
        """Finds changed rectangles between two images."""
        diff_start_time = time.monotonic()
        
        current_img_rgb = img_curr.convert('RGB') if img_curr.mode != 'RGB' else img_curr

        if img_prev is None or img_prev.size != current_img_rgb.size:
            self.metrics['frame_processing_time'].labels(stage='diff_calculation', pipeline_name=self.name).observe(time.monotonic() - diff_start_time)
            yield (0, 0, current_img_rgb.width, current_img_rgb.height)
            return

        img_prev_rgb = img_prev.convert('RGB') if img_prev.mode != 'RGB' else img_prev
        
        arr_prev = np.array(img_prev_rgb, dtype=np.int16)
        arr_curr = np.array(current_img_rgb, dtype=np.int16)

        if arr_prev.shape != arr_curr.shape:
            self._log(f"Array size mismatch during dirty_rects search: prev{arr_prev.shape}, curr{arr_curr.shape}. Sending full frame.", "WARN")
            self.metrics['frame_processing_time'].labels(stage='diff_calculation', pipeline_name=self.name).observe(time.monotonic() - diff_start_time)
            yield (0, 0, current_img_rgb.width, current_img_rgb.height)
            return

        abs_diff_arr = np.sum(np.abs(arr_curr - arr_prev), axis=2)
        changed_pixels_mask = abs_diff_arr > self._current_dynamic_threshold
        
        changed_y_coords, changed_x_coords = np.where(changed_pixels_mask)

        if changed_y_coords.size > 0:
            min_x = int(np.min(changed_x_coords))
            max_x = int(np.max(changed_x_coords))
            min_y = int(np.min(changed_y_coords))
            max_y = int(np.max(changed_y_coords))
            rect_w = max_x - min_x + 1
            rect_h = max_y - min_y + 1
            yield (min_x, min_y, rect_w, rect_h)
        
        self.metrics['frame_processing_time'].labels(stage='diff_calculation', pipeline_name=self.name).observe(time.monotonic() - diff_start_time)

    def _pack_update_packet(self, x, y, w, h, data: bytes) -> bytes:
        """Packs update data into a packet with header."""
        pack_start_time = time.monotonic()
        data_len = len(data)
        header = struct.pack('!HHHH I', x, y, w, h, data_len)
        packet = header + data
        self.metrics['frame_processing_time'].labels(stage='packet_packing', pipeline_name=self.name).observe(time.monotonic() - pack_start_time)
        self.metrics['packet_size_bytes'].labels(pipeline_name=self.name).observe(len(packet))
        return packet

    def _generator_loop(self):
        self._log("Generator thread starting.")
        
        source_mode = self.config['image_source_mode']
        sct_instance_local = None

        if source_mode == "SCREEN_CAPTURE":
            try:
                sct_instance_local = mss.mss()
                self._log("MSS (sct) instance created successfully in generator thread.")
                self._sct_instance_local_to_generator_thread = sct_instance_local
            except Exception as e:
                self._log(f"CRITICAL ERROR creating MSS in generator thread: {e}", "ERROR")
                self.pipeline_internal_stop_event.set()
                return 

        target_interval = self.config.get('generator_target_interval_sec', 0.05)
        low_water_mark = self.config.get('generator_low_water_mark', 2)

        while not self.pipeline_internal_stop_event.is_set() and not self.global_server_stop_event.is_set():
            loop_start_time = time.monotonic()
            q_size = self.frames_queue.qsize()
            self.metrics['frames_queue_size'].labels(pipeline_name=self.name).observe(q_size)

            if q_size < low_water_mark:
                gen_start_time = time.monotonic()
                generated_image: Image.Image | None = None
                metric_stage_label = f"generate_{source_mode.lower()}"
                canvas_resolution = (self.config['target_width'], self.config['target_height'])

                try:
                    if source_mode == "SCREEN_CAPTURE":
                        if sct_instance_local:
                            capture_region = self.config['capture_region']
                            sct_img = sct_instance_local.grab(capture_region)
                            generated_image = Image.frombytes('RGB', (sct_img.width, sct_img.height), sct_img.rgb, 'raw', 'RGB')
                        else:
                            self._log("MSS (sct) instance not available in SCREEN_CAPTURE generator!", "ERROR")
                            self.pipeline_internal_stop_event.set(); break 
                    elif self._generator_instance:
                        if self._generator_instance.resolution != canvas_resolution and source_mode != "PROMETHEUS_MONITOR":
                             self._log(f"Generator resolution ({source_mode}) {self._generator_instance.resolution} does not match target canvas {canvas_resolution}!", "WARN")
                        
                        bg_color_tuple = (0,0,0)
                        if hasattr(self._generator_instance, '_colors') and isinstance(self._generator_instance._colors, dict):
                            bg_color_conf = self._generator_instance._colors.get("background")
                            if isinstance(bg_color_conf, (list, tuple)) and len(bg_color_conf) == 3:
                                bg_color_tuple = tuple(bg_color_conf)

                        canvas = Image.new('RGB', self._generator_instance.resolution, color=bg_color_tuple)

                        if hasattr(self._generator_instance, 'draw_frame'):
                            self._generator_instance.draw_frame(canvas)
                        elif hasattr(self._generator_instance, 'generate_image_frame'):
                            self._generator_instance.generate_image_frame(canvas)
                        else:
                            self._log(f"Generator {type(self._generator_instance)} has no expected drawing method.", "ERROR")
                            self.pipeline_internal_stop_event.set(); break
                        generated_image = canvas
                    else:
                        self._log(f"Generator for mode '{source_mode}' is not initialized or unavailable.", "WARN")
                        generated_image = Image.new('RGB', canvas_resolution, color="red")
                        draw = ImageDraw.Draw(generated_image)
                        try: draw.text((10,10), f"Error: Gen {source_mode}", fill="white")
                        except: pass 
                        time.sleep(0.5)

                    if generated_image:
                        self.metrics['frame_processing_time'].labels(stage=metric_stage_label, pipeline_name=self.name).observe(time.monotonic() - gen_start_time)
                        self.frames_queue.put(generated_image, timeout=0.1)
                        self.metrics['frames_generated_total'].labels(pipeline_name=self.name).inc()

                except mss.exception.ScreenShotError as e:
                    self._log(f"Screen capture error: {e}. Attempting mss reinitialization...", "WARN")
                    if sct_instance_local:
                        try: sct_instance_local.close()
                        except: pass
                    try:
                        sct_instance_local = mss.mss()
                        self._sct_instance_local_to_generator_thread = sct_instance_local
                        self._log("MSS (sct) instance recreated after error.")
                    except Exception as e_reinit:
                        self._log(f"Failed to recreate MSS after error: {e_reinit}. Stopping generator.", "ERROR")
                        self.pipeline_internal_stop_event.set(); break
                    time.sleep(1.0)
                    continue
                except queue.Full:
                    pass 
                except Exception as e:
                    self._log(f"Error in generator loop (mode: {source_mode}): {e}", "ERROR")
                    import traceback
                    traceback.print_exc()
                    time.sleep(0.1)

                elapsed_this_loop = time.monotonic() - loop_start_time
                sleep_duration = target_interval - elapsed_this_loop
                if sleep_duration > 0:
                    self.pipeline_internal_stop_event.wait(sleep_duration)
            else: 
                self.pipeline_internal_stop_event.wait(0.01)

        if sct_instance_local:
            try:
                sct_instance_local.close()
                self._log("Local MSS (sct) instance closed.")
            except Exception as e:
                self._log(f"Error closing local MSS instance: {e}", "WARN")
        self._sct_instance_local_to_generator_thread = None

        self._log("Generator thread stopped.")

    def _consumer_loop(self):
        """Consumer thread loop for processing and sending frames to client."""
        self._log("Consumer thread starting.")
        self._prev_processed_image = None
        self._current_dynamic_threshold = self.config.get('min_dirty_rect_threshold', 10)
        self._frame_processing_times_history.clear()
        self._last_heartbeat_time = 0  # 心跳包发送时间戳

        self.metrics['current_dynamic_threshold'].labels(pipeline_name=self.name).set(self._current_dynamic_threshold)
        self.metrics['consumer_calculated_fps'].labels(pipeline_name=self.name).set(0)

        target_fps_val = self.config.get('target_fps', 15.0)
        history_size = self.config.get('fps_history_size', 10)
        hyst_factor = self.config.get('fps_hysteresis_factor', 0.1)
        min_thresh = self.config.get('min_dirty_rect_threshold', 5)
        max_thresh = self.config.get('max_dirty_rect_threshold', 220)
        step_up = self.config.get('threshold_adjustment_step_up', 10)
        step_down = self.config.get('threshold_adjustment_step_down', 5)
        max_chunk_data = self.config.get('max_chunk_data_size', 8192)
        heartbeat_interval = self.config.get('heartbeat_interval_sec', 2.0)  # 心跳包间隔

        while not self.pipeline_internal_stop_event.is_set() and not self.global_server_stop_event.is_set():
            try:
                raw_frame = self.frames_queue.get(timeout=0.5)
                q_size = self.frames_queue.qsize()
                self.metrics['frames_queue_size'].labels(pipeline_name=self.name).observe(q_size)

                loop_processing_start_time = time.monotonic()

                if not isinstance(raw_frame, Image.Image):
                    self._log(f"Received invalid frame type: {type(raw_frame)}. Skipping.", "WARN")
                    self.frames_queue.task_done(); continue
                
                resize_start_time = time.monotonic()
                img_resized = raw_frame.resize(
                    (self.config['target_width'], self.config['target_height']),
                    Image.Resampling.LANCZOS
                )
                self.metrics['frame_processing_time'].labels(stage='resize_thread', pipeline_name=self.name).observe(time.monotonic() - resize_start_time)

                dirty_rects_list = list(self._find_dirty_rects(self._prev_processed_image, img_resized))

                socket_error_this_frame = False
                chunks_sent_this_frame = 0
                send_duration_this_frame_start = 0

                if not dirty_rects_list and self._prev_processed_image is not None:
                    now_hb = time.monotonic()
                    if self.client_connection and (now_hb - self._last_heartbeat_time) >= heartbeat_interval:
                        try:
                            heartbeat_packet = struct.pack('!HHHH I', 0xFFFF, 0xFFFF, 0, 0, 0)
                            self.client_connection.sendall(heartbeat_packet)
                            self._last_heartbeat_time = now_hb
                        except socket.error as e:
                            self._log(f"Heartbeat send error: {e}", "WARN")
                        except Exception as e:
                            self._log(f"Heartbeat unexpected error: {e}", "WARN")
                elif dirty_rects_list:
                    self._last_heartbeat_time = time.monotonic()

                if dirty_rects_list:
                    send_duration_this_frame_start = time.monotonic()
                    for x, y, w, h in dirty_rects_list:
                        full_rect_data_size = w * h * 2
                        if full_rect_data_size > max_chunk_data:
                            bytes_per_row = w * 2
                            if bytes_per_row == 0: continue
                            chunk_h = max(1, max_chunk_data // bytes_per_row if bytes_per_row > 0 else h)
                            
                            for current_y_offset in range(0, h, chunk_h):
                                actual_chunk_h = min(chunk_h, h - current_y_offset)
                                if actual_chunk_h <= 0: continue

                                chunk_img_to_send = img_resized.crop((x, y + current_y_offset, x + w, y + current_y_offset + actual_chunk_h))
                                chunk_data_bytes = self._image_to_rgb565_bytes(chunk_img_to_send)
                                if not chunk_data_bytes: continue

                                packet_to_send = self._pack_update_packet(x, y + current_y_offset, w, actual_chunk_h, chunk_data_bytes)
                                
                                try:
                                    if not self.client_connection: raise socket.error("Client connection is None")
                                    self.client_connection.sendall(packet_to_send)
                                    chunks_sent_this_frame +=1
                                    time.sleep(0.005)
                                except socket.error as e:
                                    self._log(f"Socket error while sending chunk: {e}", "WARN")
                                    self.metrics['connection_errors_total'].labels(pipeline_name=self.name).inc()
                                    socket_error_this_frame = True; break 
                            if socket_error_this_frame: break
                        else: 
                            region_img_to_send = img_resized.crop((x,y, x+w, y+h))
                            region_data_bytes = self._image_to_rgb565_bytes(region_img_to_send)
                            if not region_data_bytes: continue

                            packet_to_send = self._pack_update_packet(x,y,w,h,region_data_bytes)
                            try:
                                if not self.client_connection: raise socket.error("Client connection is None")
                                self.client_connection.sendall(packet_to_send)
                                chunks_sent_this_frame += 1
                                time.sleep(0.005)
                            except socket.error as e:
                                self._log(f"Socket error while sending region: {e}", "WARN")
                                self.metrics['connection_errors_total'].labels(pipeline_name=self.name).inc()
                                socket_error_this_frame = True; break
                    
                    if socket_error_this_frame:
                        raise socket.error("Socket error while sending frame data") 

                    self.metrics['chunks_per_frame'].labels(pipeline_name=self.name).observe(chunks_sent_this_frame)
                    if send_duration_this_frame_start > 0 :
                        self.metrics['dirty_rects_send_duration_seconds'].labels(pipeline_name=self.name).observe(time.monotonic() - send_duration_this_frame_start)

                self._prev_processed_image = img_resized
                self.metrics['frames_processed_total'].labels(pipeline_name=self.name).inc()

                frame_total_processing_time = time.monotonic() - loop_processing_start_time
                self._frame_processing_times_history.append(frame_total_processing_time)
                
                if len(self._frame_processing_times_history) >= history_size and history_size > 0:
                    avg_time = sum(self._frame_processing_times_history) / len(self._frame_processing_times_history)
                    current_fps = 1.0 / avg_time if avg_time > 0 else 0.0
                    self.metrics['consumer_calculated_fps'].labels(pipeline_name=self.name).set(current_fps)

                    hysteresis = target_fps_val * hyst_factor
                    old_thresh = self._current_dynamic_threshold

                    if current_fps < target_fps_val - hysteresis:
                        self._current_dynamic_threshold = min(max_thresh, self._current_dynamic_threshold + step_up)
                    elif current_fps > target_fps_val + hysteresis:
                         self._current_dynamic_threshold = max(min_thresh, self._current_dynamic_threshold - step_down)
                    
                    if old_thresh != self._current_dynamic_threshold:
                        self.metrics['current_dynamic_threshold'].labels(pipeline_name=self.name).set(self._current_dynamic_threshold)
                
                self.metrics['frame_processing_time'].labels(stage='full_consumer_loop_thread', pipeline_name=self.name).observe(frame_total_processing_time)
                self.frames_queue.task_done()

            except queue.Empty:
                now = time.monotonic()
                if self.client_connection and (now - self._last_heartbeat_time) >= heartbeat_interval:
                    try:
                        heartbeat_packet = struct.pack('!HHHH I', 0xFFFF, 0xFFFF, 0, 0, 0)
                        self.client_connection.sendall(heartbeat_packet)
                        self._last_heartbeat_time = now
                    except socket.error as e:
                        self._log(f"Heartbeat send error: {e}", "WARN")
                    except Exception as e:
                        self._log(f"Heartbeat unexpected error: {e}", "WARN")
                continue
            except socket.error as e: 
                self._log(f"Socket error in consumer loop: {e}. Stopping consumer for current session.", "WARN")
                self.metrics['consumer_calculated_fps'].labels(pipeline_name=self.name).set(0)
                break 
            except Exception as e:
                self._log(f"Unexpected error in consumer loop: {e}", "ERROR")
                import traceback
                traceback.print_exc()
                self.metrics['consumer_calculated_fps'].labels(pipeline_name=self.name).set(0)
                break

        self._log("Consumer thread stopped.")

    def _cleanup_active_session(self):
        """Stops internal threads and closes client connection."""
        self._log("Cleaning up active client session...")
        if not self.pipeline_internal_stop_event.is_set():
            self.pipeline_internal_stop_event.set()

        if self._generator_thread and self._generator_thread.is_alive():
            self._log("Waiting for generator thread to stop...")
            self._generator_thread.join(timeout=2)
            if self._generator_thread.is_alive(): self._log("Generator thread did not stop.", "WARN")
        self._generator_thread = None 

        if self._consumer_thread and self._consumer_thread.is_alive():
            self._log("Waiting for consumer thread to stop...")
            self._consumer_thread.join(timeout=3) 
            if self._consumer_thread.is_alive(): self._log("Consumer thread did not stop.", "WARN")
        self._consumer_thread = None 

        if self.client_connection:
            try:
                self.client_connection.close()
                self._log("Client connection closed.")
            except Exception as e:
                self._log(f"Error closing client connection: {e}", "WARN")
        self.client_connection = None
        
        if self._generator_instance and hasattr(self._generator_instance, 'stop'):
            try:
                self._log(f"Stopping generator instance ({type(self._generator_instance).__name__})...")
                self._generator_instance.stop()
            except Exception as e: self._log(f"Error calling stop() for generator instance: {e}", "WARN")
        self._generator_instance = None

        if self._sct_instance_local_to_generator_thread:
            try:
                self._log("Closing local mss instance (if exists)...")
                self._sct_instance_local_to_generator_thread.close()
            except Exception as e: self._log(f"Error closing local sct instance: {e}", "WARN")
        self._sct_instance_local_to_generator_thread = None
        
        self._log("Clearing frames queue...")
        while not self.frames_queue.empty():
            try: self.frames_queue.get_nowait(); self.frames_queue.task_done()
            except queue.Empty: break
        self._prev_processed_image = None
        self._log("Active session cleaned up.")

    def start_pipeline_manager(self):
        """Starts the main listening and management loop in a separate thread."""
        self.manager_thread = threading.Thread(target=self._listening_loop, daemon=True, name=f"Manager_{self.name}")
        self.manager_thread.start()
        self._log("Manager thread started.")

    def stop_pipeline_manager(self):
        """Initiates full pipeline shutdown (called from outside)."""
        self._log("Received external signal for full pipeline shutdown.")
        if not self.pipeline_internal_stop_event.is_set():
            self.pipeline_internal_stop_event.set()

        if self.server_socket:
            try:
                self.server_socket.close() 
                self._log("Server (listening) socket closed to interrupt accept.")
            except Exception as e:
                self._log(f"Error closing server socket during shutdown: {e}", "WARN")
        
    def join_manager_thread(self, timeout=None):
        """Waits for the pipeline manager thread to finish (_listening_loop)."""
        if self.manager_thread and self.manager_thread.is_alive():
            self._log(f"Waiting for manager thread to finish (timeout={timeout}s)...")
            self.manager_thread.join(timeout=timeout)
            if self.manager_thread.is_alive():
                self._log(f"Manager thread did not finish within {timeout}s.", "WARN")
            else:
                self._log("Manager thread finished successfully.")

    def _listening_loop(self):
        """Main pipeline manager loop."""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.server_socket.settimeout(1.0)

        try:
            self.server_socket.bind(('', self.port))
            self.server_socket.listen(8)
            self._log(f"Listening on port {self.port} started successfully...")
        except Exception as e:
            self._log(f"CRITICAL ERROR: Failed to bind or listen on port {self.port}: {e}", "ERROR")
            if self.server_socket: self.server_socket.close() 
            return 

        while not self.global_server_stop_event.is_set():
            try:
                self.client_connection, client_address = self.server_socket.accept()
                self.client_connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.client_connection.settimeout(self.config.get('socket_timeout', 2.0))
                
                self._log(f"Client {client_address} connected successfully.")
                self.metrics['reconnections_total'].labels(pipeline_name=self.name).inc()

                self.pipeline_internal_stop_event.clear()

                if not self._initialize_generator_instance():
                    self._log("Failed to initialize generator instance. Closing connection.", "ERROR")
                    if self.client_connection: self.client_connection.close()
                    self.client_connection = None
                    continue

                self._generator_thread = threading.Thread(target=self._generator_loop, daemon=True, name=f"{self.name}_Gen")
                self._consumer_thread = threading.Thread(target=self._consumer_loop, daemon=True, name=f"{self.name}_Con")

                self._generator_thread.start()
                self._consumer_thread.start()

                while self._consumer_thread.is_alive() and not self.global_server_stop_event.is_set():
                    self._consumer_thread.join(timeout=0.2) 

                if self.global_server_stop_event.is_set():
                    self._log("Received global server stop signal during active client session.")
                elif not self._consumer_thread.is_alive(): 
                    self._log("Consumer thread finished.")
                
                self._cleanup_active_session()

            except socket.timeout: 
                continue 
            except OSError as e:
                if self.global_server_stop_event.is_set():
                    self._log(f"Socket error '{e}' during accept, likely due to server shutdown.")
                    break 
                else:
                    self._log(f"Socket error '{e}' during accept. Pausing before retry...", "ERROR")
                    time.sleep(1) 
            except Exception as e:
                self._log(f"Unexpected error in listening/connection loop: {e}", "ERROR")
                import traceback
                traceback.print_exc()
                if self.client_connection or self._consumer_thread or self._generator_thread:
                     self._cleanup_active_session()
                time.sleep(1)

        self._log("Received global stop signal or critical error. Exiting listening loop.")
        self._cleanup_active_session() 
        
        if self.server_socket:
            try:
                self.server_socket.close()
                self._log("Server (listening) socket closed successfully.")
            except Exception as e_sock:
                self._log(f"Error closing server (listening) socket: {e_sock}", "WARN")
        
        self._log("Manager thread completely stopped.")