#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

if (nrow(data) == 0) {
  quit()
}

data$Timestamp <- data$UnixTime - min(data$UnixTime)
data$IngressBps <- data$IngressBps / (1024 * 1024 * 1024)
data$EgressBps <- data$EgressBps / (1024 * 1024 * 1024)

Plot <- function(subset, output, metric, ylabel, ylimits) {
  pdf(output, height=2.5, width=5)
  p <- ggplot(subset, aes_string(x="Timestamp", y=metric, color="Node")) +
      geom_line(size=1) +
      xlab("Time (sec)") +
      ylab(ylabel) +
      coord_cartesian(ylim=ylimits) +
      theme_bw() +
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

Plot(
  data,
  paste(outpre, "ingress.pdf", sep=""),
  "IngressBps",
  "Ingress BW (Gbps)",
  c(0, 10))

Plot(
  data,
  paste(outpre, "egress.pdf", sep=""),
  "EgressBps",
  "Egress BW (Gbps)",
  c(0, 10))
