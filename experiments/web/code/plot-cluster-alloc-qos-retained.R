#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
output <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

if (nrow(data) == 0) {
  quit()
}

data$Timestamp <- data$UnixTime - min(data$UnixTime)

Plot <- function(subset, output) {
  hipri <- data.frame(
    T=subset$Timestamp,
    V=subset$FracHIPRIRetained,
    K=rep.int("HI to LOPRI", nrow(subset)))
  lopri <- data.frame(
    T=subset$Timestamp,
    V=subset$FracLOPRIRetained,
    K=rep.int("LO to HIPRI", nrow(subset)))
  d <- rbind(hipri, lopri)

  d$V <- 1 - d$V
  pdf(output, height=2.5, width=5)
  p <- ggplot(d, aes(x=T, y=V, color=K)) +
      geom_line(size=1) +
      xlab("Time (sec)") +
      ylab("Frac of hosts that change QoS") +
      coord_cartesian(ylim=c(0, 1)) +
      scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
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

Plot(data[data$FG == "AA_TO_EDGE",], output)
