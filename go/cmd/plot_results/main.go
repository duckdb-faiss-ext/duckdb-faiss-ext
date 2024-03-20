package main

import (
	"fmt"
	"image/color"
	"log"
	"os"
	"strconv"
	"strings"

	"golang.org/x/tools/benchmark/parse"
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/vg"
)

type (
	benchmarkSequence struct {
		MsPerOp [99]float64
	}
)

var colormap = map[string]color.Color{
	"filtersel": color.RGBA{R: 255, G: 0, B: 0, A: 255},
	"filterset": color.RGBA{R: 0, G: 255, B: 0, A: 255},
	"nonfilter": color.RGBA{R: 0, G: 0, B: 255, A: 255},
}

func main() {
	if len(os.Args) < 2 {
		fmt.Println("Need to have an input path")
	}
	filename := os.Args[1]
	file, err := os.Open(filename)
	if err != nil {
		fmt.Println("Unable to upen file:", err)
		return
	}
	benchmarkset, err := parse.ParseSet(file)
	if err != nil {
		fmt.Println("Unable to read benchmarks from file:", err)
		return
	}

	benchmarksData := make(map[string]benchmarkSequence, 0)

	for name, benchmarks := range benchmarkset {
		var NsPerOp float64 = 0
		for _, benchmark := range benchmarks {
			NsPerOp += benchmark.NsPerOp
		}
		NsPerOp /= float64(len(benchmarks))
		NsPerOp /= float64(len(benchmarks))

		benchmarkData := benchmarksData[parseBenchmarkMethod(name)]
		benchmarkData.MsPerOp[parseBenchmarkSelectivity(name)-1] = NsPerOp / 1000000
		benchmarksData[parseBenchmarkMethod(name)] = benchmarkData
	}

	p := plot.New()
	p.X.Min = 0
	p.Y.Min = 0
	p.X.Padding = 0
	p.Y.Padding = 0
	p.X.Label.Text = "Selectivity (%)"
	p.Y.Label.Text = "Time per batch (ms)"

	plotter.DefaultLineStyle.Width = vg.Points(1)
	plotter.DefaultGlyphStyle.Radius = vg.Points(3)
	plotter.DefaultGlyphStyle.Radius = vg.Points(3)

	p.Legend.Top = true

	for name, data := range benchmarksData {
		line, err := plotter.NewLine(data)
		if err != nil {
			log.Panic(err)
		}
		line.Color = colormap[name]
		p.Add(line)
		p.Legend.Add(name, line)
	}
	err = p.Save(500, 500, "plotLogo.png")
	if err != nil {
		log.Panic(err)
	}
}

// Implement plot.XYer for benchmarkSequence
func (s benchmarkSequence) Len() int {
	return 99
}

func (s benchmarkSequence) XY(i int) (float64, float64) {
	return float64(i + 1), s.MsPerOp[i]
}

func parseBenchmarkMethod(name string) string {
	return strings.Split(strings.Split(strings.Split(name, "/")[1], "_")[1], "-")[0]
}

func parseBenchmarkSelectivity(name string) int {
	ret, _ := strconv.Atoi(strings.Split(strings.Split(name, "/")[1], "_")[0][:2])
	return ret
}
