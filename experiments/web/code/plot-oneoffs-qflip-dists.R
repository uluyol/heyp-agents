#!/usr/bin/env Rscript

library(ggplot2)
library(reshape2)

# Derived from https://github.com/tidyverse/ggplot2/issues/1467#issuecomment-169763396
stat_myecdf <- function(mapping = NULL, data = NULL, geom = "step",
                      position = "identity", n = NULL, na.rm = FALSE,
                      show.legend = NA, inherit.aes = TRUE, direction="vh", ...) {
  layer(
    data = data,
    mapping = mapping,
    stat = StatMyecdf,
    geom = geom,
    position = position,
    show.legend = show.legend,
    inherit.aes = inherit.aes,
    params = list(
      n = n,
      na.rm = na.rm,
      direction=direction,
      ...
    )
  )
}

StatMyecdf <- ggproto("StatMyecdf", Stat,
                    compute_group = function(data, scales, n = NULL) {

                      # If n is NULL, use raw values; otherwise interpolate
                      if (is.null(n)) {
                      # Dont understand why but this version needs to sort the values
                        xvals <- sort(unique(data$x))
                      } else {
                        xvals <- seq(min(data$x), max(data$x), length.out = n)
                      }

                      y <- ecdf(data$x)(xvals)
                      x1 <- max(xvals)
                      y0 <- 0                      
                      data.frame(x = c(xvals, x1), y = c(y0, y))
                    },

                    default_aes = aes(y = ..y..),

                    required_aes = c("x")
)

args <- commandArgs(trailingOnly=TRUE)
input <- args[1]
outpre <- args[2]

data <- read.csv(input, header=TRUE, stringsAsFactors=FALSE)

Plot <- function(subset, output, metric) {
  if (nrow(subset) > 0) {
    pdf(output, height=2.5, width=5)
    p <- ggplot(subset, aes(x=Value, color=Dataset, linetype=Dataset)) +
        stat_myecdf() +
        xlab(metric) +
        ylab("CDF across connections and time") +
        coord_cartesian(ylim=c(0, 1)) +
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

for (metric in unique(data$Metric)) {
  for (fg in unique(data$FG)) {
    Plot(
      data[data$Metric == metric & data$FG == fg,],
      paste0(outpre, metric, "-", fg, ".pdf"),
      metric)
  }
}
