import warnings
warnings.filterwarnings('ignore', message=r".*doesn't match a supported version.*")

import threading
import time
import os

from config_loader import get_app_config, DEFAULT_CONFIG_FILE_PATH
from pipeline import StreamPipeline

global_server_stop_event = threading.Event()

def main():
    try:
        config_file_path = os.environ.get('APP_CONFIG_FILE', DEFAULT_CONFIG_FILE_PATH)
        print(f"[*] Loading configuration from: {config_file_path}")
        global_settings, pipeline_configurations = get_app_config(config_file_path)
    except FileNotFoundError:
        print(f"ERROR: Configuration file '{config_file_path}' not found.")
        return
    except Exception as e:
        print(f"ERROR loading configuration: {e}")
        return

    if not pipeline_configurations:
        print("No pipelines defined.")
        return

    pipeline_conf = pipeline_configurations[0]
    pipeline_name = pipeline_conf.get('name', 'ESP32_TFT_Stream')
    
    try:
        print(f"[*] Starting stream pipeline: '{pipeline_name}' on port {pipeline_conf['esp32_port']}")
        pipeline = StreamPipeline(pipeline_conf, global_server_stop_event)
        pipeline.start_pipeline_manager()
    except Exception as e:
        print(f"ERROR starting pipeline: {e}")
        return

    print("[*] Server running. Press Ctrl+C to stop.")

    try:
        while not global_server_stop_event.is_set():
            if pipeline.manager_thread and not pipeline.manager_thread.is_alive():
                print("[WARNING] Pipeline thread stopped.")
                break
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[*] Stopping server...")
    finally:
        global_server_stop_event.set()
        pipeline.stop_pipeline_manager()
        if pipeline.manager_thread:
            pipeline.manager_thread.join(timeout=5)
        print("[*] Server stopped.")

if __name__ == "__main__":
    main()
