package rtb

import (
	"golang.org/x/net/context"
)

type Request interface {
	Component(name string) interface{}
	Attach(name string, value interface{})
}

type Processor interface {
	Process(context.Context, Request) error
}

type Extractor interface {
	Extract(name ...string) interface{}
}

type Components struct {
	items map[string]interface{}
}

func (c *Components) Component(name string) interface{} {
	return c.items[name]
}

func (c *Components) Attach(name string, value interface{}) {
	if nil == c.items {
		c.items = make(map[string]interface{})
	}

	c.items[name] = value
}
