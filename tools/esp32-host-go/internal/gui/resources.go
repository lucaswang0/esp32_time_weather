//go:build !headless

package gui

import (
	_ "embed"
	"fyne.io/fyne/v2"
)

//go:embed icon.png
var iconPng []byte

// resourceIconPng is the application icon.
var resourceIconPng = &fyne.StaticResource{
	StaticName:    "icon.png",
	StaticContent: iconPng,
}