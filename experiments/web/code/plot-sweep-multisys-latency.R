#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

for (be in unique(data$BE)) {
  for (c in unique(data$C)) {
    for (y in unique(data$Y)) {
      pdf(paste(outpre, "c-", c, "-y-", y, "-be-", gsub("/", "_", be), ".pdf", sep=""), height=2.5, width=5)
      p <- ggplot(data[data$BE == be & data$C == c & data$Y == y & data$Perc == 50,], aes(x=X, y=LatencyNanos/1e6, color=factor(Sys))) +
          geom_line(size=1) +
          xlab("FG A->Edge Approval (Gbps)") +
          ylab("Latency (ms)") +
          coord_cartesian(xlim=c(0, 10), ylim=c(0, 200)) +
          scale_x_continuous(breaks=seq(0, 10, by=2)) +
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
  for (perc in c(50, 90, 95)) {
    pdf(paste(outpre, "cdf-p", perc, "-be-", gsub("/", "_", be), ".pdf", sep=""), height=2.5, width=5)
    p <- ggplot(data[data$BE == be & data$Perc == perc,], aes(x=LatencyNanos/1e6, color=factor(Sys))) +
        stat_ecdf(size=1) +
        xlab(paste("p", perc, " Latency (ms)", sep="")) +
        ylab("CDF across (X, C, Y) triples") +
        coord_cartesian(xlim=c(0, 300), ylim=c(0, 1)) +
        scale_x_continuous(breaks=seq(0, 300, by=50)) +
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
