#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(parallel)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input.approvals <- args[1]
input <- args[2]
input.truedemand <- args[3]
input.alloc <- args[4]
outpre <- args[5]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

if (nrow(data) == 0) {
  quit()
}

approvals <- read.csv(input.approvals, header=TRUE, stringsAsFactors=FALSE)
approvals$FG <- paste(approvals$SrcDC, approvals$DstDC, sep="_TO_")

allocs.state <- read.csv(input.alloc, header=TRUE, stringsAsFactors=FALSE)

minTime <- min(c(data$UnixTime, allocs.state$UnixTime))

data$Timestamp <- data$UnixTime - minTime
data$Usage <- data$Usage / (2 ^ 30)
data$Demand <- data$Demand / (2 ^ 30)

truedemand <- read.csv(input.truedemand, header=TRUE, stringsAsFactors=FALSE)
truedemand$Timestamp <- truedemand$UnixTime - minTime
truedemand$Demand <- truedemand$Demand / (2 ^ 30)

# Convert data to numeric since we might have null values.
# Suppress warnings since we deliberately want null values to convert to NAs.
allocs.state$HIPRIRateLimitBps <- suppressWarnings(as.numeric(allocs.state$HIPRIRateLimitBps))
allocs.state$LOPRIRateLimitBps <- suppressWarnings(as.numeric(allocs.state$LOPRIRateLimitBps))

allocs.state$Timestamp <- allocs.state$UnixTime - minTime
allocs.state$HIPRIRateLimitBps <- allocs.state$HIPRIRateLimitBps / (2 ^ 30)
allocs.state$LOPRIRateLimitBps <- allocs.state$LOPRIRateLimitBps / (2 ^ 30)

fgs <- unique(data$FG)

PlotFG <- function(fg, ycol, ylabel, output) {
  approval <- approvals$ApprovalBps[approvals$FG == fg]
  approval.plus.lopri <- approval + approvals$LOPRILimitBps[approvals$FG == fg]

  approval <- approval / (2 ^ 30)
  approval.plus.lopri <- approval.plus.lopri / (2 ^ 30)

  subset <- data[data$FG == fg,]
  subset$Y <- subset[[ycol]]

  subset <- aggregate(Y ~ QoS + Timestamp, FUN=sum, data=subset)
  subset$QoS <- factor(subset$QoS, levels=c("LOPRI", "HIPRI"))

  pdf(output, height=2.5, width=5)
  p <- ggplot() +
      geom_area(data=subset, aes(x=Timestamp, y=Y, fill=QoS), position="stack", alpha=0.8) +
      geom_hline(yintercept=approval, linetype="solid") +
      geom_hline(yintercept=approval.plus.lopri, linetype="dashed", color="purple") +
      geom_line(data=truedemand[truedemand$FG == fg,], aes(x=Timestamp, y=Demand), linetype="11", size=0.5) +
      xlab("Time (sec)") +
      ylab(paste(ylabel, "(Gbps)")) +
      coord_cartesian(ylim=c(0, 16)) +
      scale_y_continuous(breaks=seq(0, 16, by=2)) +
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

  if (length(approval.plus.lopri) != 0 && approval.plus.lopri > approval && sum(!is.na(allocs.state$LOPRIRateLimitBps[allocs.state$FG == fg])) > 0 && max(allocs.state$LOPRIRateLimitBps[allocs.state$FG == fg]) > 0) {
    p <- p + geom_line(aes(x=Timestamp, y=HIPRIRateLimitBps+LOPRIRateLimitBps), color="orange", data=allocs.state[allocs.state$FG == fg,])
  }

  print(p)
  .junk <- dev.off()
}

PlotFGNode <- function(fg, output) {
  subset <- data[data$FG == fg,]
  subset <- aggregate(Usage ~ Node + Timestamp, FUN=sum, data=subset)

  pdf(output, height=2.5, width=5)
  p <- ggplot(subset, aes(x=Timestamp, y=Usage, color=Node)) +
      geom_line(size=1) +
      xlab("Time (sec)") +
      ylab("Usage (Gbps)") +
      coord_cartesian(ylim=c(0, 4)) +
      scale_y_continuous(breaks=seq(0, 4, by=0.5)) +
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

tasks <- list()

for (fg in fgs) {
  tasks <- append(tasks, parallel::mcparallel(PlotFG(fg, "Usage", "Usage", paste0(outpre, "usage-ts-fg-", fg, ".pdf"))))
  tasks <- append(tasks, parallel::mcparallel(PlotFG(fg, "Demand", "Predicted Demand", paste0(outpre, "demand-ts-fg-", fg, ".pdf"))))
}

for (fg in fgs) {
  tasks <- append(tasks, parallel::mcparallel(PlotFGNode(fg, paste0(outpre, "usage-ts-fg-node-", fg, ".pdf"))))
}

.junk <- parallel::mccollect()
