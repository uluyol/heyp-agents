#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

data$Value <- data$Value / (1024 * 1024 * 1024)

data$Host[data$Metric == "Expected:HIPRI"] <- paste(data$Host[data$Metric == "Expected:HIPRI"], ":HIPRI", sep="")
data$Host[data$Metric == "Expected:LOPRI"] <- paste(data$Host[data$Metric == "Expected:LOPRI"], ":LOPRI", sep="")

data$Host[data$Metric == "Limit:HIPRI"] <- paste(data$Host[data$Metric == "Limit:HIPRI"], ":HIPRI", sep="")
data$Host[data$Metric == "Limit:LOPRI"] <- paste(data$Host[data$Metric == "Limit:LOPRI"], ":LOPRI", sep="")

data$Metric[data$Metric == "Expected:HIPRI"] <- "Expected"
data$Metric[data$Metric == "Expected:LOPRI"] <- "Expected"
data$Metric[data$Metric == "Limit:HIPRI"] <- "Limit"
data$Metric[data$Metric == "Limit:LOPRI"] <- "Limit"

hosts.all <- unique(data$Host)

hosts.hipri <- hosts.all[grepl("HIPRI", hosts.all)]
hosts.lopri <- hosts.all[grepl("LOPRI", hosts.all)]
hosts.nopri <- hosts.all[!grepl("(HI|LO)PRI", hosts.all)]

data$Host <- factor(data$Host, levels=c(hosts.nopri, hosts.hipri, hosts.lopri))

PlotFG <- function(subset, ylabel, output) {
  colors <- rainbow(length(unique(subset$Host)))
  if (sum(grepl("HIPRI", subset$Host)) > 0) {
    hipri.num <- sum(grepl("HIPRI", unique(subset$Host)))
    lopri.num <- sum(grepl("LOPRI", unique(subset$Host)))

    colors <- c(
      rev(rainbow(2*(hipri.num + lopri.num))[1:hipri.num]),
      rainbow(2*(hipri.num + lopri.num))[(hipri.num + lopri.num):(hipri.num + 2*lopri.num)])
  }

  pdf(output, height=2.5, width=5)
  p <- ggplot(subset, aes(x=Time, y=Value, fill=Host)) +
      geom_area(position="stack") +
      xlab("Time (sec)") +
      ylab(ylabel) +
      coord_cartesian(ylim=c(0, 10)) +
      theme_bw() +
      scale_fill_manual(values=colors) +
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


for (metric in unique(data$Metric)) {
  for (fg in unique(data$FG)) {
    subset <- data[data$FG == fg & data$Metric == metric,]
    PlotFG(subset, paste(metric, " (Gbps)", sep=""), paste(outpre, "fg-", fg, "-metric-", metric, ".pdf", sep=""))
  }
}