#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

data$Timestamp <- data$Timestamp - min(data$Timestamp)

if (!("NetLatencyNanosP50" %in% colnames(data))) {
  data$NetLatencyNanosP50 <- data$LatencyNanosP50
  data$NetLatencyNanosP90 <- data$LatencyNanosP90
  data$NetLatencyNanosP95 <- data$LatencyNanosP95
  data$NetLatencyNanosP99 <- data$LatencyNanosP99
}

data$NetLatencyMsP50 <- data$NetLatencyNanosP50 / 1e6
data$NetLatencyMsP90 <- data$NetLatencyNanosP90 / 1e6
data$NetLatencyMsP95 <- data$NetLatencyNanosP95 / 1e6
data$NetLatencyMsP99 <- data$NetLatencyNanosP99 / 1e6
data$GoodputGbps <- data$MeanBps / (2^30)
data$GoodputRpcs <- data$MeanRpcsPerSec / 1e3

instances <- unique(data$Instance)

Plot <- function(subset, output) {
  pdf(output, height=2.5, width=5)
  p <- ggplot(subset, aes(x=Timestamp, y=Latency, color=Kind)) +
      geom_line(size=1) +
      xlab("Time (sec)") +
      ylab("Latency (ms)") +
      coord_cartesian(ylim=c(0, 200)) +
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

PlotStacked <- function(subset, output, ylabel, ydata, ylimits) {
  pdf(output, height=2.5, width=5)
  p <- ggplot(subset, aes_string(x="Timestamp", y=ydata,
        fill="Kind")) +
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

PlotSum <- function(subset, output, ylabel, ydata, ylimits) {
  idata <- vector("list", length(instances))
  for (idx in 1:length(instances)) {
    x <- seq(0, max(subset$Timestamp))
    l <- vector("list", length(x))
    for (i in 1:length(x)) {
      l[[i]] = data.frame(
          x=x[i],
          y=sum(subset[[ydata]][subset$Instance == instances[idx] &
                                subset$Timestamp >= x[i]-0.5 &
                                subset$Timestamp < x[i]+0.5]),
          Instance=instances[idx])
    }
    idata[[idx]] <- do.call("rbind", l)
  }
  summed <- do.call("rbind", idata)
  pdf(output, height=2.5, width=5)
  p <- ggplot(summed, aes(x=x, y=y, color=Instance)) +
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

for (instance in instances) {
  subset <- data[data$Instance == instance & data$Client == "Merged",]

  tdata <- rbind(
    data.frame(
      Timestamp=subset$Timestamp,
      Kind=rep.int("p50", nrow(subset)),
      Latency=subset$NetLatencyMsP50),
    data.frame(
      Timestamp=subset$Timestamp,
      Kind=rep.int("p90", nrow(subset)),
      Latency=subset$NetLatencyMsP90),
    data.frame(
      Timestamp=subset$Timestamp,
      Kind=rep.int("p99", nrow(subset)),
      Latency=subset$NetLatencyMsP99))

  #print(tdata[1:10,])

  Plot(tdata, paste(outpre, instance, "-", "lat.pdf", sep=""))
}

PlotSum(
  data,
  paste(outpre, "bps.pdf", sep=""),
  "Mean Goodput (Gbps)",
  "GoodputGbps",
  c(0, 20))

PlotSum(
  data,
  paste(outpre, "rpcs.pdf", sep=""),
  "Mean Goodput (thousands RPCs/sec)",
  "GoodputRpcs",
  c(0, 250))

