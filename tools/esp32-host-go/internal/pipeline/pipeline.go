package pipeline

import (
	"context"
	"errors"
	"fmt"
	"image"
	"log/slog"
	"runtime/debug"
	"sync"
	"sync/atomic"
	"time"

	"esp32_host/internal/config"
	"esp32_host/internal/metrics"
	"esp32_host/internal/protocol"
	"esp32_host/internal/source"
	"esp32_host/internal/transport"
)

// Pipeline 一个完整的 source → diff → encode → send 流水线。
//
// 内部使用 3 个 goroutine：
//   - connectionLoop: 负责与 ESP32 的连接与重连
//   - generatorLoop:  抓取帧并放入 frameCh
//   - consumerLoop:   从 frameCh 取帧并处理发送
type Pipeline struct {
	cfg     config.PipelineConf
	src     source.Source
	cli     *transport.ESP32Client
	log     *slog.Logger
	metrics *metrics.PipelineMetrics

	// 运行期状态
	frameCh chan *image.NRGBA
	prevImg *image.NRGBA
	resized *image.NRGBA
	thresh  *ThresholdController

	mu     sync.Mutex
	active bool
	stop   context.CancelFunc
	wg     sync.WaitGroup

	// 外部 ctx 取消时停止
	parentCtx context.Context
}

// New 构造 pipeline；source 必须在外面准备好。
func New(cfg config.PipelineConf, src source.Source, disc *transport.Discovery, log *slog.Logger) (*Pipeline, error) {
	if log == nil {
		log = slog.Default()
	}
	cli, err := transport.NewESP32Client(cfg, disc, log)
	if err != nil {
		return nil, err
	}
	m := metrics.New()
	return &Pipeline{
		cfg:     cfg,
		src:     src,
		cli:     cli,
		log:     log.With("subsystem", "pipeline", "pipeline", cfg.Name),
		metrics: m,
		frameCh: make(chan *image.NRGBA, cfg.Queue.MaxSize),
		thresh: NewThresholdController(
			cfg.Diff.MinThreshold,
			cfg.Diff.MaxThreshold,
			cfg.Diff.StepUp,
			cfg.Diff.TargetFPS,
			cfg.Diff.Hysteresis,
			cfg.Diff.StepUp,
			cfg.Diff.StepDown,
			cfg.Queue.LowWaterMark,
		),
	}, nil
}

// Metrics 返回内部 metrics（用于 GUI 状态栏/日志面板）。
func (p *Pipeline) Metrics() *metrics.PipelineMetrics { return p.metrics }

// Start 启动 connectionLoop。返回的 error 仅来自初始 cli 构造失败。
// 后续连接错误会通过日志输出并自动重试，不会让 Start 失败。
func (p *Pipeline) Start(parent context.Context) error {
	p.mu.Lock()
	if p.active {
		p.mu.Unlock()
		return errors.New("pipeline: already started")
	}
	p.active = true
	p.mu.Unlock()

	ctx, cancel := context.WithCancel(parent)
	p.stop = cancel

	p.wg.Add(1)
	go p.connectionLoop(ctx)
	return nil
}

// Stop 取消所有 goroutine 并等待退出。
func (p *Pipeline) Stop() {
	p.mu.Lock()
	if !p.active {
		p.mu.Unlock()
		return
	}
	p.active = false
	cancel := p.stop
	p.mu.Unlock()
	if cancel != nil {
		cancel()
	}
	p.wg.Wait()
	p.src.Close()
	p.cli.Close()
}

// connectionLoop 负责与 ESP32 的连接管理：
//   - 试连 → 成功则启动 generator+consumer
//   - consumer 结束（连接断开）→ 关闭连接 → 等待 reconnect_interval → 重试
//
// 重连退避采用指数退避（1s → 2s → 4s … 上限 30s），重连成功后重置。
// 任何 loop 内的 panic 都会被 recover 并记入 metrics，避免整个进程崩溃。
func (p *Pipeline) connectionLoop(ctx context.Context) {
	defer p.wg.Done()
	defer p.recoverLoop("connectionLoop")

	reconnect := p.cfg.ReconnectInterval()
	backoff := time.Second // 指数退避起点
	const maxBackoff = 30 * time.Second

	for {
		select {
		case <-ctx.Done():
			p.log.Info("connection loop: context cancelled, exiting")
			return
		default:
		}

		p.log.Info("connecting to ESP32", "host_or_auto", p.cfg.ESP32.Host != "")
		if err := p.cli.Connect(ctx); err != nil {
			p.log.Warn("connect failed, will retry", "err", err, "after", backoff)
			p.metrics.ConnectionErrors.Add(1)
			if !sleepCtx(ctx, backoff) {
				return
			}
			backoff *= 2
			if backoff > maxBackoff {
				backoff = maxBackoff
			}
			continue
		}
		p.metrics.Reconnections.Add(1)
		p.log.Info("connected, starting generator + consumer", "remote", p.cli.RemoteAddr())
		backoff = time.Second // 重连成功后重置退避

		// sessionCtx 仅在本次连接生命周期内有效；连接断开时取消 consumer/generator
		sessionCtx, sessionCancel := context.WithCancel(ctx)

		var sessionWg sync.WaitGroup
		sessionWg.Add(2)
		go func() {
			defer sessionWg.Done()
			defer p.recoverLoop("generatorLoop")
			p.generatorLoop(sessionCtx)
		}()
		go func() {
			defer sessionWg.Done()
			defer p.recoverLoop("consumerLoop")
			p.consumerLoop(sessionCtx)
		}()

		// 等待 consumer 结束或外部 ctx 取消
		doneCh := make(chan struct{})
		go func() { sessionWg.Wait(); close(doneCh) }()
		select {
		case <-doneCh:
			p.log.Warn("session ended, will reconnect", "after", reconnect)
		case <-ctx.Done():
			p.log.Info("parent ctx cancelled, stopping session")
		}
		sessionCancel()
		sessionWg.Wait()
		p.cli.Close()
		// 复位内部缓存
		p.prevImg = nil

		if ctx.Err() != nil {
			return
		}
		if !sleepCtx(ctx, reconnect) {
			return
		}
	}
}

// recoverLoop 在 goroutine panic 时记录日志 + 计数，让 connectionLoop 看到 session 结束并触发重连。
// 永远不应再次 panic；这里用 slog + atomic.Int64，失败也只是少打一条日志。
func (p *Pipeline) recoverLoop(name string) {
	r := recover()
	if r == nil {
		return
	}
	p.metrics.PanicRecovered.Add(1)
	// 截断长 stack 避免日志膨胀
	stack := debug.Stack()
	if len(stack) > 2048 {
		stack = stack[:2048]
	}
	p.log.Error("recovered from panic in "+name,
		"panic", fmt.Sprintf("%v", r),
		"stack", string(stack),
	)
}

// generatorLoop 抓帧并入队。限速 + 低水位与 Python 版等价。
func (p *Pipeline) generatorLoop(ctx context.Context) {
	lowWater := p.cfg.Queue.LowWaterMark
	if lowWater <= 0 {
		lowWater = 1
	}
	targetInterval := time.Duration(p.cfg.Diff.GeneratorRate * float64(time.Second))

	for {
		select {
		case <-ctx.Done():
			p.log.Info("generator: context done")
			return
		default:
		}

		loopStart := time.Now()
		// 仅当队列容量低于低水位时才生成新帧
		if len(p.frameCh) < lowWater {
			t := time.Now()
			frame, err := p.src.Next(ctx)
			if err != nil {
				p.metrics.SourceErrors.Add(1)
				p.log.Warn("source.Next failed", "err", err)
				if !sleepCtx(ctx, 200*time.Millisecond) {
					return
				}
				continue
			}
			p.metrics.FramesGenerated.Add(1)
			p.metrics.RecordSample(time.Since(t).Seconds() * 0) // 占位，consumer 计算真正的帧时间
			_ = t

			// 尝试入队；队列满则丢弃本次（与 Python queue.put timeout=0.1 行为相似）
			select {
			case p.frameCh <- frame:
			default:
				p.log.Debug("frame dropped (queue full)")
			}
		}

		elapsed := time.Since(loopStart)
		if rest := targetInterval - elapsed; rest > 0 {
			if !sleepCtx(ctx, rest) {
				return
			}
		}
	}
}

// consumerLoop 处理并发送一帧：resize → diff → crop → color → rgb565 → send
func (p *Pipeline) consumerLoop(ctx context.Context) {
	// 预分配目标图像
	tw, th := p.cfg.Target.Width, p.cfg.Target.Height
	if p.resized == nil || p.resized.Bounds().Dx() != tw || p.resized.Bounds().Dy() != th {
		p.resized = image.NewNRGBA(image.Rect(0, 0, tw, th))
	}
	lastHeartbeat := time.Now()
	heartbeatInterval := time.Duration(p.cfg.HeartbeatInterval() /* see note */)
	_ = heartbeatInterval
	heartbeatInt := 2 * time.Second // 兜底（pipeline 未单独配置时与 global 对齐）

	for {
		select {
		case <-ctx.Done():
			p.log.Info("consumer: context done")
			return
		default:
		}

		// 阻塞等帧，超时 500ms
		var frame *image.NRGBA
		select {
		case frame = <-p.frameCh:
		case <-time.After(500 * time.Millisecond):
			// 超时：检查是否需要发心跳
			if time.Since(lastHeartbeat) >= heartbeatInt {
				if p.cli.IsConnected() {
					if err := p.cli.SendHeartbeat(); err == nil {
						lastHeartbeat = time.Now()
					} else {
						p.log.Warn("heartbeat send failed", "err", err)
						return // 退出 consumer → connectionLoop 重连
					}
				}
			}
			continue
		}

		loopStart := time.Now()
		// 1) resize 到目标尺寸（CatmullRom ≈ LANCZOS）
		ResizeInto(p.resized, frame, KernelCatmullRom)

		// 2) diff → 脏矩形
		rect := FindDirtyRect(p.prevImg, p.resized, p.thresh.Current())

		// 3) 发送或心跳
		if !rect.Changed && p.prevImg != nil {
			if time.Since(lastHeartbeat) >= heartbeatInt {
				if p.cli.IsConnected() {
					if err := p.cli.SendHeartbeat(); err == nil {
						lastHeartbeat = time.Now()
					} else {
						p.metrics.SendErrors.Add(1)
						p.log.Warn("heartbeat send failed", "err", err)
						return
					}
				}
			}
		} else if rect.Changed {
			// 4) crop → color → rgb565 → send
			if err := p.sendDirtyRect(p.resized, rect); err != nil {
				p.metrics.SendErrors.Add(1)
				p.log.Warn("send dirty rect failed", "err", err)
				return
			}
			lastHeartbeat = time.Now()
		}

		p.savePrev()
		p.metrics.FramesProcessed.Add(1)
		p.thresh.Record(time.Since(loopStart))
		p.metrics.SetFPS(p.thresh.LastFPS())
	}
}

// savePrev 把当前帧拷贝到 prevImg（独立缓冲）。
// 必须拷贝而不能指针赋值：resized 会被下一帧原地覆盖（ResizeInto），
// 若 prevImg 与 resized 共享底层数组，diff 将变成自己和自己比较，恒为无变化。
func (p *Pipeline) savePrev() {
	if p.prevImg == nil || p.prevImg.Bounds() != p.resized.Bounds() {
		p.prevImg = image.NewNRGBA(p.resized.Bounds())
	}
	copy(p.prevImg.Pix, p.resized.Pix)
}

// sendDirtyRect 把一个 (x,y,w,h) 矩形从 src 中取出，做颜色校正 + RGB565，
// 然后通过 transport.ESP32Client 发送（自动按 8192 字节切分）。
func (p *Pipeline) sendDirtyRect(src *image.NRGBA, r DirtyRect) error {
	if r.W <= 0 || r.H <= 0 {
		return fmt.Errorf("sendDirtyRect: invalid rect %+v", r)
	}
	// 1) crop 拷贝到独立 buffer（不污染 src，便于 prev 复用）
	crop := image.NewNRGBA(image.Rect(0, 0, r.W, r.H))
	srcStride := src.Stride
	for y := 0; y < r.H; y++ {
		srcStart := (r.Y+y)*srcStride + r.X*4
		srcEnd := srcStart + r.W*4
		dstStart := y * crop.Stride
		copy(crop.Pix[dstStart:dstStart+r.W*4], src.Pix[srcStart:srcEnd])
	}

	// 2) 颜色校正（gamma + wb_scale）— 就地修改 crop
	protocol.ApplyGammaAndWhiteBalance(crop, p.cfg.Colors.Gamma, p.cfg.Colors.WBScale)

	// 3) RGB565 编码
	rgb, err := protocol.EncodeRGB565(crop, 0, 0, r.W, r.H)
	if err != nil {
		return err
	}

	// 4) 发送（自动分块）
	if err := p.cli.SendRect(uint16(r.X), uint16(r.Y), uint16(r.W), uint16(r.H), rgb); err != nil {
		return err
	}
	p.metrics.ChunksSent.Add(int64(protocol.ChunkCount(r.W, r.H, transport.MaxChunkData)))
	return nil
}

// sleepCtx 在 ctx 取消时立即返回 false。
func sleepCtx(ctx context.Context, d time.Duration) bool {
	if d <= 0 {
		return ctx.Err() == nil
	}
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return false
	case <-t.C:
		return true
	}
}

// 静默未使用 import / atomic
var (
	_ atomic.Bool
	_ sync.Mutex
)
