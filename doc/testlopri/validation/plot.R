#!/usr/bin/env Rscript

library(ggplot2)
library(wesanderson)

got.const <- read.csv("got-interarrival-const.csv", header=T, stringsAsFactors=F)
got.uni <- read.csv("got-interarrival-uni.csv", header=T, stringsAsFactors=F)
got.exp <- read.csv("got-interarrival-exp.csv", header=T, stringsAsFactors=F)

expected.const <- read.csv("expected-interarrival-const.csv", header=T, stringsAsFactors=F)
expected.uni <- read.csv("expected-interarrival-uni.csv", header=T, stringsAsFactors=F)
expected.exp <- read.csv("expected-interarrival-exp.csv", header=T, stringsAsFactors=F)

got.const$Kind <- rep.int("Observed", nrow(got.const))
expected.const$Kind <- rep.int("Expected", nrow(expected.const))
data.const <- rbind(got.const, expected.const)

got.uni$Kind <- rep.int("Observed", nrow(got.uni))
expected.uni$Kind <- rep.int("Expected", nrow(expected.uni))
data.uni <- rbind(got.uni, expected.uni)


got.exp$Kind <- rep.int("Observed", nrow(got.exp))
expected.exp$Kind <- rep.int("Expected", nrow(expected.exp))
data.exp <- rbind(got.exp, expected.exp)

pdf("interarrival-const.pdf", height=2.5, width=5)
ggplot(data.const, aes(x=Value/1e3, y=Percentile/100, color=Kind)) +
    geom_step() +
    xlab("Time between requests (us)") +
    ylab("CDF across requests") +
    coord_cartesian(
        ylim=c(0, 1.1),
        xlim=c(0, 2*max(data.const$Value[data.const$Kind == "Expected"]) / 1000.0)) +
    scale_y_continuous(breaks=c(0, 0.2, 0.4, 0.6, 0.8, 1), expand=c(0, 0)) +
    theme_bw()
.junk <- dev.off()

pdf("interarrival-exp.pdf", height=2.5, width=5)
ggplot(data.exp, aes(x=Value/1e3, y=Percentile/100, color=Kind)) +
    geom_step() +
    xlab("Time between requests (us)") +
    ylab("CDF across requests") +
    coord_cartesian(
        ylim=c(0, 1.1),
        xlim=c(0, 2*max(data.exp$Value[data.exp$Kind == "Expected"]) / 1000.0)) +
    scale_y_continuous(breaks=c(0, 0.2, 0.4, 0.6, 0.8, 1), expand=c(0, 0)) +
    theme_bw()
.junk <- dev.off()

pdf("interarrival-uni.pdf", height=2.5, width=5)
ggplot(data.uni, aes(x=Value/1e3, y=Percentile/100, color=Kind)) +
    geom_step() +
    xlab("Time between requests (us)") +
    ylab("CDF across requests") +
    coord_cartesian(
        ylim=c(0, 1.1),
        xlim=c(0, 2*max(data.uni$Value[data.uni$Kind == "Expected"]) / 1000.0)) +
    scale_y_continuous(breaks=c(0, 0.2, 0.4, 0.6, 0.8, 1), expand=c(0, 0)) +
    theme_bw()
.junk <- dev.off()
