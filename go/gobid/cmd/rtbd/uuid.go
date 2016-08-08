// Copyright (c) 2016 Datacratic. All rights reserved.
package main

import (
	"crypto/rand"
	"encoding/binary"
	"log"
	"sync/atomic"
)

var uuid uint64

func init() {
	_, err := rand.Read(seed[:])
	if err != nil {
		log.Fatal(err)
	}
}

const (
	digits = "0123456789abcdef"
)

var seed [16]byte

func NewUUID() string {
	u := atomic.AddUint64(&uuid, 1)
	var ram [8]byte
	binary.LittleEndian.PutUint64(ram[:], u)
	id := seed
	for i, j := range ram {
		id[(uint64(i))%16] ^= j
	}

	var s [37]byte
	i := 0
	j := 0

	write := func(n int) {
		s[i] = '-'
		i++

		for k := 0; k < n; k++ {
			x := id[j]
			s[i] = digits[x%16]
			i++
			s[i] = digits[x/16]
			i++
			j++
		}
	}

	write(4)
	write(2)
	write(2)
	write(2)
	write(6)

	return string(s[1:])
}

func NewUUIDs(n int) []string {
	ids := make([]string, n)

	for i := 0; i < n; i++ {
		ids[i] = NewUUID()
	}

	return ids
}
