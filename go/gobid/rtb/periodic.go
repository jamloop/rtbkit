package rtb

import (
	"fmt"
	"time"
)

type Scheduler interface {
	// Schedule returns if and when the next event should be scheduled.
	Schedule() (t time.Time, ok bool)
}

func Every(period time.Duration) *PeriodicSchedule {
	return &PeriodicSchedule{
		Period: period,
	}
}

type PeriodicSchedule struct {
	Period time.Duration
	last   time.Time
}

func (s *PeriodicSchedule) Schedule() (t time.Time, ok bool) {
	t, ok = s.last, true

	if t.IsZero() {
		t = time.Now().UTC().Truncate(s.Period)
	}

	t = t.Add(s.Period)

	s.last = t
	return
}

func (s *PeriodicSchedule) At(when string) (at *AtSchedule) {
	// 00:00
	if len(when) == 5 && when[2] == ':' {
		when = fmt.Sprintf("%sh%sm", when[:2], when[3:])
	}

	d, err := time.ParseDuration(when)
	if err != nil {
		return
	}

	at = &AtSchedule{
		Parent: s,
		Offset: d,
	}

	return
}

type AtSchedule struct {
	Parent Scheduler
	Offset time.Duration
}

func (s *AtSchedule) Schedule() (t time.Time, ok bool) {
	t, ok = s.Parent.Schedule()
	if ok {
		t = t.Add(s.Offset)
	}

	return
}

type Runner interface {
	Run()
}

type RunnerFunc func()

func (f RunnerFunc) Run() {
	f()
}

func Periodic(s Scheduler, r Runner) (done chan struct{}) {
	done = make(chan struct{})
	go func() {
		for {
			now := time.Now()

			t, ok := s.Schedule()
			if !ok {
				return
			}

			if t.After(now) {
				select {
				case <-time.After(t.Sub(now)):
				case <-done:
					return
				}
			}

			r.Run()
		}
	}()

	return
}

func PeriodicFunc(s Scheduler, f func()) (done chan struct{}) {
	return Periodic(s, RunnerFunc(f))
}
