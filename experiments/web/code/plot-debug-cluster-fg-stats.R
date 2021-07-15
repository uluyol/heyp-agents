#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)
data$Time <- round(data$Time / 5) * 5
data$Value[data$Metric == "AllocatedLimit"] <- data$Value[data$Metric == "AllocatedLimit"] / (1024*1024*1024)
data$Value[data$Metric == "ExpectedUsage"] <- data$Value[data$Metric == "ExpectedUsage"] / (1024*1024*1024)

PlotStacked <- function(subset, output, ylabel, ylimits) {
  pdf(output, height=5, width=5)
  p <- ggplot(subset, aes_string(x="Time", y="Value", fill="Kind")) +
      geom_area(position="stack", alpha=0.8) +
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

PlotStacked(
  data[data$Metric == "AllocatedLimit",],
  paste(outpre, "allocatedlimits.pdf", sep=""),
  "Allocated Limit (Gbps)",
  c(0, 25))

PlotStacked(
  data[data$Metric == "ExpectedUsage",],
  paste(outpre, "expectedusages.pdf", sep=""),
  "Expected Usage (Gbps)",
  c(0, 25))
