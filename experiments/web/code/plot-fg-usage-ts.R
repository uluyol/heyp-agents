#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input.approvals <- args[1]
input <- args[2]
input.alloc <- args[3]
outpre <- args[4]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

if (nrow(data) == 0) {
  quit()
}

approvals <- read.csv(input.approvals, header=TRUE, stringsAsFactors=FALSE)
approvals$FG <- paste(approvals$SrcDC, approvals$DstDC, sep="_TO_")

allocs.state <- read.csv(input.alloc, header=TRUE, stringsAsFactors=FALSE)

minTime <- min(c(data$UnixTime, allocs.state$UnixTime))

data$Timestamp <- data$UnixTime - minTime
data$Usage <- data$Usage / (1024 * 1024 * 1024)
data$Demand <- data$Demand / (1024 * 1024 * 1024)

allocs.state$Timestamp <- allocs.state$UnixTime - minTime
allocs.state$HIPRIRateLimitBps <- allocs.state$HIPRIRateLimitBps / (1024 * 1024 * 1024)
allocs.state$LOPRIRateLimitBps <- allocs.state$LOPRIRateLimitBps / (1024 * 1024 * 1024)

fgs <- unique(data$FG)

PlotFG <- function(subset, fg, ycol, ylabel, output) {
  approval <- approvals$ApprovalBps[approvals$FG == fg]
  approval.plus.lopri <- approval + approvals$LOPRILimitBps[approvals$FG == fg]

  approval <- approval / (1024 * 1024 * 1024)
  approval.plus.lopri <- approval.plus.lopri / (1024 * 1024 * 1024)

  subset$Y <- subset[[ycol]]

  subset <- aggregate(Y ~ QoS + Timestamp, FUN=sum, data=subset)
  subset$QoS <- factor(subset$QoS, levels=c("LOPRI", "HIPRI"))

  pdf(output, height=2.5, width=5)
  p <- ggplot() +
      geom_area(data=subset, aes(x=Timestamp, y=Y, fill=QoS), position="stack", alpha=0.8) +
      geom_hline(yintercept=approval, linetype="solid") +
      geom_hline(yintercept=approval.plus.lopri, linetype="dashed", color="purple") +
      xlab("Time (sec)") +
      ylab(paste(ylabel, "(Gbps)")) +
      coord_cartesian(ylim=c(0, 10)) +
      scale_y_continuous(breaks=seq(0, 10, by=2)) +
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

  if (approval.plus.lopri > approval && nrow(allocs.state[allocs.state$FG == fg,]) > 0 && max(allocs.state$LOPRIRateLimitBps[allocs.state$FG == fg]) > 0) {
    p <- p + geom_line(aes(x=Timestamp, y=HIPRIRateLimitBps+LOPRIRateLimitBps), color="orange", data=allocs.state[allocs.state$FG == fg,])
  }

  print(p)
  .junk <- dev.off()
}

PlotFGNode <- function(subset, output) {
  subset <- aggregate(Usage ~ Node + Timestamp, FUN=sum, data=subset)

  pdf(output, height=2.5, width=5)
  p <- ggplot(subset, aes(x=Timestamp, y=Usage, color=Node)) +
      geom_line(size=1) +
      xlab("Time (sec)") +
      ylab("Usage (Gbps)") +
      coord_cartesian(ylim=c(0, 10)) +
      scale_y_continuous(breaks=seq(0, 10, by=2)) +
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

PlotAgg <- function(subset, output) {
  subset <- aggregate(Usage ~ FG + Timestamp, FUN=sum, data=subset)

  pdf(output, height=2.5, width=5)
  p <- ggplot(subset, aes(x=Timestamp, y=Usage, color=FG)) +
      geom_line(size=1) +
      xlab("Time (sec)") +
      ylab("Usage (Gbps)") +
      coord_cartesian(ylim=c(0, 0)) +
      scale_y_continuous(breaks=seq(0, 10, by=2)) +
      theme_bw() +
      guides(color=guide_legend(ncol=2)) +
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

for (fg in fgs) {
  subset <- data[data$FG == fg,]
  PlotFG(subset, fg, "Usage", "Usage", paste0(outpre, "usage-ts-fg-", fg, ".pdf"))
  PlotFG(subset, fg, "Demand", "Predicted Demand", paste0(outpre, "demand-ts-fg-", fg, ".pdf"))
}

for (fg in fgs) {
  subset <- data[data$FG == fg,]
  PlotFGNode(subset, paste0(outpre, "usage-ts-fg-node-", fg, ".pdf"))
}

PlotAgg(
  data,
  paste0(outpre, "usage-ts-agg.pdf"))
