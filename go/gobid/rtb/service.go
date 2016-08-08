package rtb

import "io"

type Service interface {
	Start() error
}

type Services []Service

func (s Services) Start() (err error) {
	for i := range s {
		if err = s[i].Start(); err != nil {
			return
		}
	}

	return
}

func (s Services) Close() (err error) {
	for i := range s {
		c, ok := s[i].(io.Closer)
		if !ok {
			continue
		}

		if err = c.Close(); err != nil {
			return
		}
	}

	return
}
