#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)
library(wesanderson)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
output <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

data$Time <- data$Step * 2 # seconds

long <- melt(data, id.vars=c("Flow", "Step", "Time"), variable.name="Kind")
long$value <- long$value / (1024 * 1024)

long <- long[!is.nan(long$value),]
long <- long[long$Kind != "Recv",]
#long <- long[long$Kind != "SSDemand",]

print(long)

pdf(output, height=8, width=7)
ggplot(long, aes(x=Time, y=value, color=Kind, linetype=Kind)) +
    geom_line(size=1) +
    xlab("Time (sec)") +
    ylab("Mbps") +
    facet_wrap(~ Flow, ncol=3) +
    coord_cartesian(ylim=c(0, 800), xlim=c(0, 30)) +
    scale_x_continuous(breaks=c(0, 100, 200, 300, 400, 500, 600, 700, 800)) +
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
.junk <- dev.off()
