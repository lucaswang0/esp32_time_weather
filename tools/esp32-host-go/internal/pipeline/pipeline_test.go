package pipeline

import (
	"image"
	"testing"
)

// TestSavePrev_NotAliased 回归测试：prevImg 必须是独立缓冲。
// 若 prevImg 与 resized 共享底层数组，ResizeInto 原地覆盖后 diff 恒为无变化，
// 表现为只发送首帧（chunks 卡在初始帧数量）。
func TestSavePrev_NotAliased(t *testing.T) {
	p := &Pipeline{resized: image.NewNRGBA(image.Rect(0, 0, 320, 170))}

	p.savePrev()
	if p.prevImg == nil {
		t.Fatal("prevImg not allocated")
	}
	if &p.prevImg.Pix[0] == &p.resized.Pix[0] {
		t.Fatal("prevImg shares backing array with resized")
	}

	// 修改 resized 不应影响 prevImg
	p.resized.Pix[0] = 0xFF
	if p.prevImg.Pix[0] == 0xFF {
		t.Fatal("prevImg aliased to resized: modifying resized changed prevImg")
	}
}

// TestSavePrev_CopiesContent 验证 savePrev 拷贝完整像素内容。
func TestSavePrev_CopiesContent(t *testing.T) {
	p := &Pipeline{resized: image.NewNRGBA(image.Rect(0, 0, 4, 2))}
	for i := range p.resized.Pix {
		p.resized.Pix[i] = uint8(i % 251)
	}

	p.savePrev()
	for i := range p.resized.Pix {
		if p.prevImg.Pix[i] != p.resized.Pix[i] {
			t.Fatalf("prevImg[%d]=%d, want %d", i, p.prevImg.Pix[i], p.resized.Pix[i])
		}
	}

	// 再次 savePrev（prevImg 已存在）应更新内容而不是复用旧缓冲失败
	for i := range p.resized.Pix {
		p.resized.Pix[i] = 0
	}
	p.savePrev()
	if p.prevImg.Pix[0] != 0 {
		t.Fatal("second savePrev did not update prevImg content")
	}
}
