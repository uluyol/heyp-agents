#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)
colnames(data) <- c("Dataset", "FG", "Metric", "Stat", "Value")
baseline <- data[data$Dataset == "stableqos_oversub", c("FG", "Metric", "Stat", "Value")]
colnames(baseline) <- c("FG", "Metric", "Stat", "Baseline")

data <- merge(data, baseline)
data$NormValue <- data$Value / data$Baseline
data$NormValue[data$Value == data$Baseline] <- 1

Plot <- function(subset, output, stat) {
  if (nrow(subset) > 0) {
    pdf(output, height=5, width=15)
    p <- ggplot(subset, aes(x=Metric, y=NormValue, fill=Dataset)) +
        geom_bar(stat="identity", position="dodge", width=0.5) +
        xlab("") +
        ylab(paste0(stat, " with X / ", stat, " with stableqos_oversub")) +
        coord_cartesian(ylim=c(0, 3.5)) +
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
            axis.text.x=element_text(color="black", size=11, angle = 90, vjust = 0.5, hjust=1),
            axis.title.y=element_text(size=12, margin=margin(0, 3, 0, 0)),
            axis.title.x=element_blank())
    print(p)
    .junk <- dev.off()
  }
}

for (fg in unique(data$FG)) {
  for (stat in unique(data$Stat)) {
    Plot(
      data[data$FG == fg & data$Stat == stat,],
      paste0(outpre, fg, "-", stat, ".pdf"),
      stat)
  }
}
