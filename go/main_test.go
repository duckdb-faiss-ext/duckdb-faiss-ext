package main

import (
	"fmt"
	"math"
	"runtime"
	"testing"

	_ "github.com/ianlancetaylor/cgosymbolizer"
	"gonum.org/v1/gonum/stat/distuv"
)

const targetPass = 0.99

func BenchmarkAllQueriesIVF2048_10(b *testing.B) {
	const requiredResults = 10

	benchinit()

	for p := 1; p < 100; p++ {
		p := p
		binom := distuv.Binomial{
			P: float64(p) / 100,
		}
		requiredN, _ := bisectRootN(func(x float64) float64 {
			binom.N = x
			return (1 - binom.CDF(requiredResults)) - targetPass
		}, 0, 100000)

		b.Run(fmt.Sprintf("%02d%%_nonfilter", p), func(b *testing.B) {
			b.ReportMetric(float64(requiredN), "#queries")
			runtime.LockOSThread()
			benchrun_non(uint64(b.N), uint32(requiredN))
			runtime.UnlockOSThread()
		})
		b.Run(fmt.Sprintf("%02d%%_filtersel", p), func(b *testing.B) {
			runtime.LockOSThread()
			benchrun_sel(uint64(b.N), uint32(p))
			runtime.UnlockOSThread()
		})
		b.Run(fmt.Sprintf("%02d%%_filterset", p), func(b *testing.B) {
			runtime.LockOSThread()
			benchrun_set(uint64(b.N), uint32(p))
			runtime.UnlockOSThread()
		})
	}
}

func bisectRootN(f func(float64) float64, min, max int64) (int64, error) {
	l := f(float64(min))
	r := f(float64(max))
	if max == min+1 {
		if math.Abs(l) < math.Abs(r) {
			return min, nil
		} else {
			return max, nil
		}
	}

	m := f(float64((min + max) / 2))
	if m == 0.0 {
		if 0 < r {
			return bisectRootN(f, (min+max)/2, max)
		} else {
			return bisectRootN(f, min, (min+max)/2)
		}
	} else if math.Signbit(m) == math.Signbit(r) {
		return bisectRootN(f, min, (min+max)/2)
	} else if math.Signbit(m) == math.Signbit(l) {
		return bisectRootN(f, (min+max)/2, max)
	}
	return 0, fmt.Errorf("input should be of opposite sign!")
}
