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

my_theme <- function() {
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
        axis.title.y=element_text(size=12, margin=margin(0, 5, 0, 0)),
        axis.title.x=element_text(size=12, margin=margin(5, 0, 0, 0)))
}

PlotDowngradeFracError <- function(subset, mult.lb, metric.name, metric, output) {
    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.downgradeSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        xlab(paste0(metric.name, " downgrade frac w/ exact demand - w/ estimate")) +
        ylab("CDF across instances") +
        coord_cartesian(xlim=c(0.05 * mult.lb, 0.05), ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
        my_theme()
    print(p)
    .junk <- dev.off()
}

PlotDowngradeFracErrorByHostUsagesGen <- function(subset, mult.lb, metric.name, metric, output) {
    pdf(output, height=4.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.downgradeSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        xlab(paste0(metric.name, " downgrade frac w/ exact demand - w/ estimate")) +
        ylab("CDF across instances") +
        facet_wrap(~ hostUsagesGen, ncol=2) +
        coord_cartesian(xlim=c(0.05 * mult.lb, 0.05), ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
        my_theme()
    print(p)
    .junk <- dev.off()
}

PlotUsageNormErrorByHostUsagesGen <- function(subset, metric.name, metric, output) {
    pdf(output, height=4.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.samplerSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        facet_wrap(~ hostUsagesGen, ncol=2) +
        xlab(paste0(metric.name, " (estimated - exact usage) / exact usage")) +
        ylab("CDF across instances") +
        coord_cartesian(xlim=c(-1, 1), ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
        my_theme()
    print(p)
    .junk <- dev.off()
}

PlotRateLimitNormErrorByHostUsagesGen <- function(subset, metric.name, metric, output) {
    pdf(output, height=4.5, width=5)
    p <- ggplot(data=subset, aes_string(
            x=paste0("sys.rateLimitSummary.", metric),
            color="sys.samplerName")) +
        stat_myecdf(size=1) +
        facet_wrap(~ hostUsagesGen, ncol=2) +
        xlab(paste0(metric.name, " (host limit with estimated usage - with exact usage) / with exact usage")) +
        ylab("CDF across instances") +
        coord_cartesian(xlim=c(-1, 1), ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
        my_theme()
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
        xlab("Mean number of samples") +
        ylab("CDF across instances") +
        facet_wrap(~ numHosts, ncol=2) +
        coord_cartesian(ylim=c(0, 1)) +
        scale_y_continuous(breaks=seq(0, 1, by=0.2)) +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
        my_theme()
    print(p)
    .junk <- dev.off()
}

PlotMeanNumSamplesOverExpected <- function(subset, output) {
    data <- rbind(
        data.frame(x=subset$sys.samplerSummary.meanNumSamples / pmin(subset$numSamplesAtApproval, subset$numHosts),
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
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
        my_theme()
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

PlotUsageNormErrorByHostUsagesGen(data, "Mean", "meanUsageAbsNormError", file.path(outdir, "usage-abs-norm-error-hug-mean.pdf"))
PlotUsageNormErrorByHostUsagesGen(data, "5%ile", "usageAbsNormErrorPerc.p5", file.path(outdir, "usage-abs-norm-error-hug-p5.pdf"))
PlotUsageNormErrorByHostUsagesGen(data, "95%ile", "usageAbsNormErrorPerc.p95", file.path(outdir, "usage-abs-norm-error-hug-p95.pdf"))

PlotDowngradeFracError(data, 0, "Mean abs", "meanIntendedFracAbsError", file.path(outdir, "downgrade-frac-abs-error-mean.pdf"))
PlotDowngradeFracError(data, 0, "95%ile abs", "intendedFracAbsErrorPerc.p95", file.path(outdir, "downgrade-frac-abs-error-p95.pdf"))

PlotDowngradeFracErrorByHostUsagesGen(data, -1, "Mean", "meanIntendedFracError", file.path(outdir, "downgrade-frac-error-hug-mean.pdf"))
PlotDowngradeFracErrorByHostUsagesGen(data, -1, "5%ile", "intendedFracErrorPerc.p5", file.path(outdir, "downgrade-frac-error-hug-p5.pdf"))
PlotDowngradeFracErrorByHostUsagesGen(data, -1, "95%ile", "intendedFracErrorPerc.p95", file.path(outdir, "downgrade-frac-error-hug-p95.pdf"))

PlotDowngradeFracErrorByHostUsagesGen(data, 0, "Mean abs", "meanIntendedFracAbsError", file.path(outdir, "downgrade-frac-abs-error-hug-mean.pdf"))
PlotDowngradeFracErrorByHostUsagesGen(data, 0, "95%ile abs", "intendedFracAbsErrorPerc.p95", file.path(outdir, "downgrade-frac-abs-error-hug-p95.pdf"))

PlotRateLimitNormErrorByHostUsagesGen(data, "Mean", "meanNormError", file.path(outdir, "host-limit-norm-error-hug-mean.pdf"))
PlotRateLimitNormErrorByHostUsagesGen(data, "5%ile", "normErrorPerc.p5", file.path(outdir, "host-limit-norm-error-hug-p5.pdf"))
PlotRateLimitNormErrorByHostUsagesGen(data, "95%ile", "normErrorPerc.p95", file.path(outdir, "host-limit-norm-error-hug-p95.pdf"))

PlotMeanNumSamplesOverExpected(data, file.path(outdir, "num-samples-over-expected-mean.pdf"))
PlotMeanNumSamplesByRequested(data, file.path(outdir, "num-samples-by-req.pdf"))
