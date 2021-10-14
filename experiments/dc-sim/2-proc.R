#!/usr/bin/env Rscript

library(methods)
library(ggplot2)
library(jsonlite)

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

PlotDowngradeFracError <- function(subset, metric, output) {
    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.samplerSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        xlab("Downgrade Frac with Exact Demand - Frac With Estimate") +
        ylab("CDF across instances") +
        coord_cartesian(xlim=c(-0.05, 0.05), ylim=c(0, 1)) +
        scale_x_continuous(breaks=seq(-0.05, 0.05, by=0.025)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        theme_bw() +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
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

PlotDowngradeFracErrorByHostUsagesGen <- function(subset, metric, output) {
    pdf(output, height=4.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.samplerSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        xlab("Downgrade Frac with Exact Demand - Frac With Estimate") +
        ylab("CDF across instances") +
        facet_wrap(~ hostUsagesGen, ncol=2) +
        coord_cartesian(xlim=c(-0.05, 0.05), ylim=c(0, 1)) +
        scale_x_continuous(breaks=seq(-0.05, 0.05, by=0.025)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        theme_bw() +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
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

PlotUsageErrorFrac <- function(subset, metric, output) {
    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.samplerSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        xlab("(Exact - Estimate Usage) / Exact") +
        ylab("CDF across instances") +
        coord_cartesian(xlim=c(-0.2, 0.2), ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        theme_bw() +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
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

PlotUsageErrorFracByHostUsagesGen <- function(subset, metric, output) {
    pdf(output, height=4.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.samplerSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        facet_wrap(~ hostUsagesGen, ncol=2) +
        xlab("(Exact - Estimate Usage) / Exact") +
        ylab("CDF across instances") +
        coord_cartesian(xlim=c(-0.5, 0.5), ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        theme_bw() +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
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

PlotUsageErrorFracByAOD <- function(subset, metric, output) {
    pdf(output, height=4.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.samplerSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        facet_wrap(~ approvalOverExpectedUsage, ncol=2) +
        xlab("(Exact - Estimate Usage) / Exact") +
        ylab("CDF across instances") +
        coord_cartesian(xlim=c(-0.5, 0.5), ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        theme_bw() +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
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

PlotMeanNumSamplesByRequested <- function(subset, output) {
    measured <- subset[, c("numHosts", "sys.samplerName", "sys.samplerSummary.meanNumSamples")]
    intended <- unique(subset[, c("instanceID", "numHosts", "numSamplesAtApproval")])
    intended$numSamplesAtApproval <- pmin(intended$numSamplesAtApproval, intended$numHosts)
    data <- rbind(
        data.frame(numHosts=measured$numHosts, kind=measured$sys.samplerName, num=measured$sys.samplerSummary.meanNumSamples),
        data.frame(numHosts=intended$numHosts, kind=rep.int("wantAtApproval", nrow(intended)), num=intended$numSamplesAtApproval))
    pdf(output, height=4.5, width=5)
    p <- ggplot(data=data, aes(x=num, color=kind)) +
        stat_myecdf(size=1) +
        xlab("Number of samples") +
        ylab("CDF across instances") +
        facet_wrap(~ numHosts, ncol=2) +
        coord_cartesian(ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        theme_bw() +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
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

PlotMeanNumSamplesOverExpected <- function(subset, output) {
    data <- rbind(
        data.frame(x=subset$sys.samplerSummary.meanNumSamples / subset$numSamplesAtApproval,
                   kind=subset$sys.samplerName),
        data.frame(x=subset$approvalOverExpectedUsage[subset$sys.samplerName == "weighted"],
                   kind=rep.int("weighted (expected)", sum(subset$sys.samplerName == "weighted"))))
    pdf(output, height=2.5, width=5)
    p <- ggplot(data=data, aes(x=x, color=kind)) +
        stat_myecdf(size=1) +
        xlab("Mean number of samples / requested at approval") +
        ylab("CDF across instances") +
        coord_cartesian(ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        theme_bw() +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
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

args <- commandArgs(trailingOnly=TRUE)

if (length(args) != 2) {
    stop("usage: ./2-proc.R data.json outdir")
}

simresults <- args[1]
outdir <- args[2]

unlink(outdir, recursive=TRUE)
dir.create(outdir, recursive=TRUE)

con <- file(simresults, open="r")
data <- stream_in(con, flatten=TRUE, verbose=FALSE)
close(con)

PlotUsageErrorFrac(data, "meanUsageErrorFrac", file.path(outdir, "usage-error-frac-mean.pdf"))
PlotUsageErrorFrac(data, "usageErrorFracPerc.p5", file.path(outdir, "usage-error-frac-p5.pdf"))
PlotUsageErrorFrac(data, "usageErrorFracPerc.p95", file.path(outdir, "usage-error-frac-p95.pdf"))

PlotUsageErrorFracByHostUsagesGen(data, "meanUsageErrorFrac", file.path(outdir, "usage-error-frac-hug-mean.pdf"))
PlotUsageErrorFracByHostUsagesGen(data, "usageErrorFracPerc.p5", file.path(outdir, "usage-error-frac-hug-p5.pdf"))
PlotUsageErrorFracByHostUsagesGen(data, "usageErrorFracPerc.p95", file.path(outdir, "usage-error-frac-hug-p95.pdf"))

PlotUsageErrorFracByAOD(data, "meanUsageErrorFrac", file.path(outdir, "usage-error-frac-aod-mean.pdf"))
PlotUsageErrorFracByAOD(data, "usageErrorFracPerc.p5", file.path(outdir, "usage-error-frac-aod-p5.pdf"))
PlotUsageErrorFracByAOD(data, "usageErrorFracPerc.p95", file.path(outdir, "usage-error-frac-aod-p95.pdf"))

PlotDowngradeFracError(data, "meanDowngradeFracError", file.path(outdir, "downgrade-frac-error-hug-mean.pdf"))
PlotDowngradeFracError(data, "downgradeFracErrorPerc.p5", file.path(outdir, "downgrade-frac-error-hug-p5.pdf"))
PlotDowngradeFracError(data, "downgradeFracErrorPerc.p95", file.path(outdir, "downgrade-frac-error-hug-p95.pdf"))

PlotDowngradeFracErrorByHostUsagesGen(data, "meanDowngradeFracError", file.path(outdir, "downgrade-frac-error-mean.pdf"))
PlotDowngradeFracErrorByHostUsagesGen(data, "downgradeFracErrorPerc.p5", file.path(outdir, "downgrade-frac-error-p5.pdf"))
PlotDowngradeFracErrorByHostUsagesGen(data, "downgradeFracErrorPerc.p95", file.path(outdir, "downgrade-frac-error-p95.pdf"))

PlotMeanNumSamplesOverExpected(data, file.path(outdir, "num-samples-over-expected-mean.pdf"))
PlotMeanNumSamplesByRequested(data, file.path(outdir, "num-samples-by-req.pdf"))
