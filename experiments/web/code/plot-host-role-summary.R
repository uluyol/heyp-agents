#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

data$Value[data$Metric == "IngressBW" | data$Metric == "EgressBW"] <-
  data$Value[data$Metric == "IngressBW" | data$Metric == "EgressBW"] / 
    (1024 * 1024 * 1024)

data <- data[data$Role != "host-agent",]

Plot <- function(subset, output, ylabel, ylimits) {
  subset <- subset[subset$Stat == "Max",]

  if (nrow(subset) > 0) {
    pdf(output, height=2.5, width=5)
    p <- ggplot(subset, aes(x=Role, y=Value)) +
        geom_bar(stat="identity", width=0.5) +
        xlab("Node Role") +
        ylab(ylabel) +
        scale_fill_manual(values=colors) +
        coord_flip(ylim=ylimits) +
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
}

Plot(
  data[data$Metric == "IngressBW",],
  paste(outpre, "IngressBW.pdf", sep=""),
  "Max Ingress BW (Gbps)",
  c(0, 10))

Plot(
  data[data$Metric == "EgressBW",],
  paste(outpre, "EgressBW.pdf", sep=""),
  "Max Egress BW (Gbps)",
  c(0, 10))

Plot(
  data[data$Metric == "CPU",],
  paste(outpre, "CPU.pdf", sep=""),
  "Max CPU Utilization (%)",
  c(0, 100))

