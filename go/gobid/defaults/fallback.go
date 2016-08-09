package defaults

import "time"

// String is a simple helper to provide a fallback string when empty.
func String(value, fallback string) string {
	if value == "" {
		return fallback
	}

	return value
}

// Duration is a simple helper to provide a fallback duration when zero.
func Duration(value, fallback time.Duration) time.Duration {
	if value == 0 {
		return fallback
	}

	return value
}
