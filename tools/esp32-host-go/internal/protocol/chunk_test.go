package protocol

import (
	"reflect"
	"testing"
)

// TestChunkRect_FitsInOneChunk 整个矩形能装进一个包时应返回单个矩形。
// 100x40 = 8000 字节 < 8192，应整体作为 1 个包。
func TestChunkRect_FitsInOneChunk(t *testing.T) {
	got := ChunkRect(0, 0, 100, 40, 8192)
	if len(got) != 1 {
		t.Fatalf("expected 1 chunk, got %d: %+v", len(got), got)
	}
	if got[0] != (Rect{X: 0, Y: 0, W: 100, H: 40}) {
		t.Fatalf("unexpected rect: %+v", got[0])
	}
}

// TestChunkRect_MultipleChunks 验证与 Python 版等价的切分行为。
//
// Python pipeline.py 等价计算：
//
//	bytes_per_row = 320 * 2 = 640
//	chunk_h = 8192 // 640 = 12
//	for off in range(0, 240, 12): sub_h = min(12, 240 - off)
//
// → 12+12+...+12 = 240
// 共 20 个 chunk，每个高 12。
func TestChunkRect_MultipleChunks(t *testing.T) {
	got := ChunkRect(0, 0, 320, 240, 8192)
	want := []Rect{}
	for off := 0; off < 240; off += 12 {
		want = append(want, Rect{X: 0, Y: off, W: 320, H: 12})
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("chunks mismatch:\n got  %+v\n want %+v", got, want)
	}
}

// TestChunkRect_HeightNotMultiple 当 h 不是 chunkH 的整数倍时，最后一个块应截断。
func TestChunkRect_HeightNotMultiple(t *testing.T) {
	// 100x23, maxChunkData=200 → bytesPerRow=200, chunkH=1 → 23 个 1 行块
	got := ChunkRect(0, 0, 100, 23, 200)
	if len(got) != 23 {
		t.Fatalf("expected 23 chunks, got %d", len(got))
	}
	if got[len(got)-1].H != 1 {
		t.Fatalf("last chunk height = %d, want 1", got[len(got)-1].H)
	}
}

// TestChunkRect_TinyChunkH 验证 chunkH=1 边界。
func TestChunkRect_TinyChunkH(t *testing.T) {
	// 100x3, maxChunkData=100 → bytesPerRow=200 > 100 → chunkH=1 → 3 chunks
	got := ChunkRect(0, 0, 100, 3, 100)
	if len(got) != 3 {
		t.Fatalf("expected 3 chunks, got %d: %+v", len(got), got)
	}
	for i, r := range got {
		if r.Y != i || r.H != 1 {
			t.Fatalf("chunk %d unexpected: %+v", i, r)
		}
	}
}

// TestChunkRect_NeedsSplit 与 Python pipeline.py 等价：100x50 = 10000 > 8192 应切 2 块。
func TestChunkRect_NeedsSplit(t *testing.T) {
	got := ChunkRect(0, 0, 100, 50, 8192)
	want := []Rect{
		{X: 0, Y: 0, W: 100, H: 40},
		{X: 0, Y: 40, W: 100, H: 10},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("chunks mismatch:\n got  %+v\n want %+v", got, want)
	}
}

// TestChunkCount 与 ChunkRect 数量保持一致。
func TestChunkCount(t *testing.T) {
	cases := []struct {
		w, h, max int
		want      int
	}{
		{100, 40, 8192, 1},   // 8000 < 8192, 1 chunk
		{100, 50, 8192, 2},   // 10000 > 8192, bytesPerRow=200, chunkH=40, 40+10
		{320, 240, 8192, 20}, // Python 等价
		{100, 23, 200, 23},   // bytesPerRow=200, max=200, chunkH=1
		{0, 0, 100, 0},
	}
	for _, c := range cases {
		if got := ChunkCount(c.w, c.h, c.max); got != c.want {
			t.Errorf("ChunkCount(%d,%d,%d) = %d, want %d", c.w, c.h, c.max, got, c.want)
		}
	}
}
