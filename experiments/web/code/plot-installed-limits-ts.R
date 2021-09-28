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
data$LimitGbps <- data$LimitBps / (1024 * 1024 * 1024)
data$QoS[data$QoS == "CRITICAL"] <- "HIPRI" # TODO: consider distinguishing these
data$Kind <- paste(data$Node, ":", data$QoS, sep="")

kinds.all <- unique(data$Kind)

kinds.hipri <- kinds.all[grepl("HIPRI", kinds.all)]
kinds.lopri <- kinds.all[grepl("LOPRI", kinds.all)]
kinds.nopri <- kinds.all[!grepl("(HI|LO)PRI", kinds.all)]

data$Kind <- factor(data$Kind, levels=c(kinds.nopri, kinds.hipri, kinds.lopri))

Plot <- function(subset, output) {
  colors <- rainbow(length(unique(subset$Kind)))
  if (sum(grepl("HIPRI", subset$Kind)) > 0) {
    hipri.num <- sum(grepl("HIPRI", unique(subset$Kind)))
    lopri.num <- sum(grepl("LOPRI", unique(subset$Kind)))

    colors <- c(
      rev(rainbow(2*(hipri.num + lopri.num))[1:hipri.num]),
      rainbow(2*(hipri.num + lopri.num))[(hipri.num + lopri.num):(hipri.num + 2*lopri.num)])
  }

  summed <- aggregate(LimitGbps ~ Timestamp + Kind, FUN=sum, data=subset)

  pdf(output, height=2.5, width=5)
  p <- ggplot(summed, aes(x=Timestamp, y=LimitGbps, fill=Kind)) +
      geom_area(position="stack", alpha=0.8) +
      xlab("Time (sec)") +
      ylab("Limit (Gbps)") +
      scale_fill_manual(values=colors) +
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

for (src in unique(data$SrcDC)) {
  for (dst in unique(data$DstDC)) {
    subset <- data[data$SrcDC == src & data$DstDC == dst,]
    if (nrow(subset) > 0) {
      Plot(subset, paste(outpre, src, "_TO_", dst, ".pdf", sep=""))
    }
  }
}
