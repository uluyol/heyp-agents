#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)
data$BE = paste(data$Group, data$Instance, sep="_")

for (wlconfig in unique(data$WLConfig)) {
  for (be in unique(data$BE)) {
    pdf(paste(outpre, wlconfig, "-be-", be, ".pdf", sep=""), height=2.5, width=5)
    p <- ggplot(data[data$BE == be & data$WLConfig == wlconfig,], aes(x=LatencyNanos/1e6, y=CumNumSamples, color=Sys, linetype=Sys)) +
      geom_line(size=1) +
      xlab("Latency = L (ms)") +
      ylab("# of reqs with latency < L") +
      coord_cartesian(xlim=c(0, 300)) +
      scale_x_continuous(breaks=seq(0, 300, by=25)) +
      theme_bw() +
      guides(color=guide_legend(ncol=3)) +
      theme(
        legend.title=element_blank(),
        legend.position="top",
        legend.margin=margin(0, 0, 0, 0),
        legend.box.margin=margin(-4, -4, -8, 0),
        legend.background=element_rect(color="black", fill="white", linetype="blank", size=0),
        legend.direction="horizontal",
        legend.key=element_blank(),
        legend.key.height=unit(11, "points"),
        legend.key.width=unit(25, "points"),
        legend.spacing.x=unit(1, "points"),
        legend.spacing.y=unit(0, "points"),
        legend.text=element_text(size=11, margin=margin(r=10)),
        strip.background=element_rect(color="white", fill="white"),
        strip.text=element_text(size=12),
        plot.margin=unit(c(5.5, 8.5, 5.5, 5.5), "points"),
        axis.text=element_text(color="black", size=11),
        axis.title.y=element_text(size=12, margin=margin(0, 3, 0, 0)),
        axis.title.x=element_text(size=12, margin=margin(3, 0, 0, 0)))
    print(p)
    .junk <- dev.off()
  }
}
