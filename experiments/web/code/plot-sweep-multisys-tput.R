#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

data$NormalizedGoodput <- data$ActualGbps / data$X
data$NormalizedGoodput[data$BE == "G/BE2"] <- data$ActualGbps[data$BE == "G/BE2"] / data$Y[data$BE == "G/BE2"]

for (c in unique(data$C)) {
    for (y in unique(data$Y)) {
        pdf(paste(outpre, "c-", c, "-y-", y, ".pdf", sep=""), height=2.5, width=5)
        p <- ggplot(data[data$C == c & data$Y == y,], aes(x=X, y=NormalizedGoodput, color=Sys, linetype=BE)) +
            geom_line(size=1) +
            geom_line(data=data.frame(x=c(0, 10), y=c(0, 10)), aes(x=x, y=y), color="black", linetype="dashed", size=0.5) +
            xlab("FG A->Edge Approval (Gbps)") +
            ylab("Goodput / Approval (Gbps)") +
            coord_cartesian(xlim=c(0, 10), ylim=c(0, 10)) +
            scale_x_continuous(breaks=seq(0, 10, by=2)) +
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
}


pdf(paste(outpre, "cdf.pdf", sep=""), height=2.5, width=5)
p <- ggplot(data, aes(x=NormalizedGoodput, color=Sys, linetype=BE)) +
    stat_ecdf(size=1) +
    xlab("Goodput / Approval (Gbps)") +
    ylab("CDF across (X, C, Y) triples") +
    coord_cartesian(xlim=c(0, 2), ylim=c(0, 1)) +
    scale_x_continuous(breaks=seq(0, 2, by=0.25)) +
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
