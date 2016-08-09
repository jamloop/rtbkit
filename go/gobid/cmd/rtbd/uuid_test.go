// Copyright (c) 2016 Datacratic. All rights reserved.
package main

import "testing"

func TestNewUUID(t *testing.T) {
	list := make(map[string]struct{})
	n := 1 << 20

	for i := 0; i < n; i++ {
		id := NewUUID()
		_, ok := list[id]
		if ok {
			t.Fail()
		}

		if i == 0 {
			t.Log("e.g.", id)
		}
	}

	t.Log(n, "unique UUID")
}

func BenchmarkNewUUID(b *testing.B) {
	for i := 0; i < b.N; i++ {
		NewUUID()
	}
}
