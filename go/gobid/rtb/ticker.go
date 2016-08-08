package rtb

import (
	"time"

	"golang.org/x/net/context"
)

// Ticker implements a tick function.
type Ticker interface {
	Tick(context.Context)
}

// Tick returns a channel that can be closed to stop the tick function.
func Tick(d time.Duration, f func()) chan struct{} {
	t := time.NewTicker(d)
	c := make(chan struct{})
	go func() {
		for {
			select {
			case <-t.C:
				f()
			case <-c:
				t.Stop()
				return
			}
		}
	}()

	return c
}
