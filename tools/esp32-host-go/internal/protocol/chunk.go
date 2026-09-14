package protocol

import "math"

// ChunkRect 把一个 (x, y, w, h) 矩形按 maxChunkData 切分为多个 (x', y', w, h') 子矩形。
//
// 切分规则与 Python pipeline.py 中 _consumer_loop 等价：
//
//	bytesPerRow = w * 2
//	if bytesPerRow == 0: continue
//	chunkH = max(1, maxChunkData // bytesPerRow if bytesPerRow > 0 else h)
//	for yOff in range(0, h, chunkH): subH = min(chunkH, h - yOff)
//
// maxChunkData 应等于 ESP32 StreamingPlayerPage.h 中的 MAX_CHUNK_SIZE (8192)。
func ChunkRect(x, y, w, h int, maxChunkData int) []Rect {
	if w <= 0 || h <= 0 {
		return nil
	}
	if maxChunkData <= 0 {
		return nil
	}

	bytesPerRow := w * 2
	if bytesPerRow == 0 {
		return nil
	}

	total := w * h * 2
	if total <= maxChunkData {
		return []Rect{{X: x, Y: y, W: w, H: h}}
	}

	chunkH := maxChunkData / bytesPerRow
	if chunkH <= 0 {
		chunkH = 1
	}

	var out []Rect
	for off := 0; off < h; off += chunkH {
		subH := chunkH
		if off+subH > h {
			subH = h - off
		}
		out = append(out, Rect{X: x, Y: y + off, W: w, H: subH})
	}
	return out
}

// Rect 一个子矩形。
type Rect struct {
	X, Y, W, H int
}

// ChunkCount 仅返回切分数量，用于在编码前预估包数。
func ChunkCount(w, h, maxChunkData int) int {
	if w <= 0 || h <= 0 || maxChunkData <= 0 {
		return 0
	}
	bytesPerRow := w * 2
	if bytesPerRow == 0 {
		return 0
	}
	total := w * h * 2
	if total <= maxChunkData {
		return 1
	}
	chunkH := max(1, maxChunkData/bytesPerRow)
	return int(math.Ceil(float64(h) / float64(chunkH)))
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
