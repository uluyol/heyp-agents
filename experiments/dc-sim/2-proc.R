#!/usr/bin/env Rscript

library(methods)
library(ggplot2)
library(jsonlite)
library(parallel)

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

PlotOverOrShortageVersusSamples <- function(subset, metric.name, output) {
    x <- aggregate(sys.samplerSummary.numSamples.mean ~ numSamplesAtApproval, data=subset, FUN=sum)
    if (nrow(x) != nrow(subset)) {
        print(subset)
        stop(paste0("found ", nrow(subset) - nrow(x), " duplicate entries in data"))
    }

    long <- rbind(
        data.frame(
            Kind=rep.int("QD-Intended", nrow(subset)),
            NumSamples=subset$numSamplesAtApproval,
            OverOrShortage=subset[[paste0("sys.downgradeSummary.intendedOverOrShortage.", metric.name)]]),
        data.frame(
            Kind=rep.int("QD-Realized", nrow(subset)),
            NumSamples=subset$numSamplesAtApproval,
            OverOrShortage=subset[[paste0("sys.downgradeSummary.realizedOverOrShortage.", metric.name)]]),
        data.frame(
            Kind=rep.int("RateLimit", nrow(subset)),
            NumSamples=subset$numSamplesAtApproval,
            OverOrShortage=subset[[paste0("sys.rateLimitSummary.overOrShortage.", metric.name)]]))

    pdf(output, height=4.5, width=5)
    p <- ggplot(data=long, aes(x=NumSamples, y=OverOrShortage, color=Kind)) +
        geom_line(size=1) +
        ylab(paste0(metric.name, " |got - want| / want")) +
        xlab("# of samples collected at approval (powers of two)") +
        scale_x_continuous(trans="log1p",
            breaks=c(0, 8, 64, 512, 4096, 32768, 262144, 2097152),
            labels=c("0", "8", "64", "512", "4K", "32K", "256K", "2M"),
            limits=c(0, NA)) +
        coord_cartesian(ylim=c(0, 0.2)) +
        guides(color=guide_legend(ncol=3), linetype=guide_legend(ncol=3)) +
        my_theme()
    print(p)
    .junk <- dev.off()
}

AppendAllPlotOverOrShortageVersusSamples <- function(tasklist, subset, metric.name, outdir) {
    dir.create(outdir, recursive=TRUE)
    for (nhosts in unique(subset$numHosts)) {
        for (hug in unique(subset$hostUsagesGen)) {
            for (aoe in unique(subset$approvalOverExpectedUsage)) {
                # PlotOverOrShortageVersusSamples(
                #         subset[
                #             subset$numHosts == nhosts &
                #             subset$hostUsagesGen == hug &
                #             subset$approvalOverExpectedUsage == aoe,],
                #         metric.name, file.path(outdir, paste0("nhosts:", nhosts, ":hug:", hug, ":aoe:", aoe, ".pdf")))
                tasklist <- append(tasklist, parallel::mcparallel(
                    PlotOverOrShortageVersusSamples(
                        subset[
                            subset$numHosts == nhosts &
                            subset$hostUsagesGen == hug &
                            subset$approvalOverExpectedUsage == aoe,],
                        metric.name, file.path(outdir, paste0("nhosts:", nhosts, ":hug:", hug, ":aoe:", aoe, ".pdf")))))
            }
        }
    }
    tasklist
}

PlotMeanNumSamplesVersusRequested <- function(subset, output) {
    measured <- subset[, c("numSamplesAtApproval", "sys.samplerName", "sys.samplerSummary.numSamples.mean")]
    means <- aggregate(sys.samplerSummary.numSamples.mean ~ numSamplesAtApproval + sys.samplerName, data=subset, FUN=mean)
    pdf(output, height=2.5, width=5)
    p <- ggplot(data=means, aes(x=numSamplesAtApproval, y=sys.samplerSummary.numSamples.mean, color=sys.samplerName)) +
        geom_line(size=1) +
        xlab("Configured # of samples at approval") +
        ylab("Mean # of samples collected") +
        guides(color=guide_legend(ncol=3)) +
        my_theme()
    print(p)
    .junk <- dev.off()
}

PlotMeanNumSamplesByRequested <- function(subset, output) {
    measured <- subset[, c("numHosts", "sys.samplerName", "sys.samplerSummary.numSamples.mean")]
    # intended <- unique(subset[, c("instanceID", "numHosts", "numSamplesAtApproval")])
    # intended$numSamplesAtApproval <- pmin(intended$numSamplesAtApproval, intended$numHosts)
    # intended.weighted <- unique(subset[, c("instanceID", "approvalOverExpectedUsage", "numHosts", "numSamplesAtApproval")])
    # intended.weighted$numSamples <- pmin(intended.weighted$approvalOverExpectedUsage * intended$numSamplesAtApproval, intended$numHosts)
    data <- data.frame(numHosts=measured$numHosts, kind=measured$sys.samplerName, num=measured$sys.samplerSummary.numSamples.mean)
    # data <- rbind(
    #     data.frame(numHosts=measured$numHosts, kind=measured$sys.samplerName, num=measured$sys.samplerSummary.numSamples.mean),
    #     data.frame(numHosts=intended.weighted$numHosts, kind=rep.int("wantWeighted", nrow(intended.weighted)), num=intended.weighted$numSamples),
    #     data.frame(numHosts=intended$numHosts, kind=rep.int("wantAtApproval", nrow(intended)), num=intended$numSamplesAtApproval))
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

# Disabled. Hard to interpret. See PlotMeanNumSamplesVersusRequested for something more intuitive.
PlotMeanNumSamplesOverExpected <- function(subset, output) {
    isWeightedSamplerMask <- subset$sys.samplerName == "weighted"
    wantSamplesAtApproval <- pmin(subset$numSamplesAtApproval, subset$numHosts)
    numSamplesWantWeighted <- pmin(subset$numHosts[isWeightedSamplerMask],
        subset$approvalOverExpectedUsage[isWeightedSamplerMask] * subset$numSamplesAtApproval[isWeightedSamplerMask])
    numSamplesWantWeightedOverRequested <- numSamplesWantWeighted / wantSamplesAtApproval[isWeightedSamplerMask]

    data <- data.frame(x=subset$sys.samplerSummary.numSamples.mean / wantSamplesAtApproval, kind=subset$sys.samplerName)
    # data <- rbind(
    #     data.frame(x=subset$sys.samplerSummary.numSamples.mean / wantSamplesAtApproval,
    #                kind=subset$sys.samplerName),
    #     data.frame(x=subset$sys.samplerSummary.wantNumSamples.mean / wantSamplesAtApproval,
    #                kind=paste0(subset$sys.samplerName, " (wanted)")))
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

if (length(args) != 1) {
    stop("usage: ./2-proc.R resdir")
}

resdir <- args[1]
simresults <- file.path(resdir, "sim-data.json")
outdir <- file.path(resdir, "proc")

unlink(outdir, recursive=TRUE)
dir.create(outdir, recursive=TRUE)

con <- file(simresults, open="r")
data <- stream_in(con, flatten=TRUE, verbose=FALSE)
close(con)

tasks <- list(
    # UsageNormError (HUG)
    parallel::mcparallel(
        PlotUsageNormErrorByHostUsagesGen(data, "Mean", "absUsageNormError.mean",
            file.path(outdir, "usage-abs-norm-error-hug-mean.pdf"))),
    parallel::mcparallel(
        PlotUsageNormErrorByHostUsagesGen(data, "5%ile", "absUsageNormError.p5",
            file.path(outdir, "usage-abs-norm-error-hug-p5.pdf"))),
    parallel::mcparallel(
        PlotUsageNormErrorByHostUsagesGen(data, "95%ile", "absUsageNormError.p95",
            file.path(outdir, "usage-abs-norm-error-hug-p95.pdf"))),
    # IntendedFracError
    parallel::mcparallel(
        PlotDowngradeFracError(data, 0, "Mean abs", "absIntendedFracError.mean",
            file.path(outdir, "downgrade-frac-abs-error-mean.pdf"))),
    parallel::mcparallel(
        PlotDowngradeFracError(data, 0, "95%ile abs", "absIntendedFracError.p95",
            file.path(outdir, "downgrade-frac-abs-error-p95.pdf"))),
    # IntededFracError (HUG)
    parallel::mcparallel(
        PlotDowngradeFracErrorByHostUsagesGen(data, -1, "Mean", "intendedFracError.mean",
            file.path(outdir, "downgrade-frac-error-hug-mean.pdf"))),
    parallel::mcparallel(
        PlotDowngradeFracErrorByHostUsagesGen(data, -1, "5%ile", "intendedFracError.p5",
            file.path(outdir, "downgrade-frac-error-hug-p5.pdf"))),
    parallel::mcparallel(
        PlotDowngradeFracErrorByHostUsagesGen(data, -1, "95%ile", "intendedFracError.p95",
            file.path(outdir, "downgrade-frac-error-hug-p95.pdf"))),
    parallel::mcparallel(
        PlotDowngradeFracErrorByHostUsagesGen(data, 0, "Mean abs", "absIntendedFracError.mean",
            file.path(outdir, "downgrade-frac-abs-error-hug-mean.pdf"))),
    parallel::mcparallel(
        PlotDowngradeFracErrorByHostUsagesGen(data, 0, "95%ile abs", "absIntendedFracError.p95",
            file.path(outdir, "downgrade-frac-abs-error-hug-p95.pdf"))),
    # RateLimitNormError
    parallel::mcparallel(
        PlotRateLimitNormErrorByHostUsagesGen(data, "Mean", "normError.mean",
            file.path(outdir, "host-limit-norm-error-hug-mean.pdf"))),
    parallel::mcparallel(
        PlotRateLimitNormErrorByHostUsagesGen(data, "5%ile", "normError.p5",
            file.path(outdir, "host-limit-norm-error-hug-p5.pdf"))),
    parallel::mcparallel(
        PlotRateLimitNormErrorByHostUsagesGen(data, "95%ile", "normError.p95",
            file.path(outdir, "host-limit-norm-error-hug-p95.pdf"))),
    # NumSamples
    # parallel::mcparallel(
    #     PlotMeanNumSamplesOverExpected(data,
    #         file.path(outdir, "num-samples-over-expected-mean.pdf"))),
    parallel::mcparallel(
        PlotMeanNumSamplesVersusRequested(data,
            file.path(outdir, "num-samples-vs-req.pdf"))),
    parallel::mcparallel(
        PlotMeanNumSamplesByRequested(data,
            file.path(outdir, "num-samples-by-req.pdf"))))

tasks <- AppendAllPlotOverOrShortageVersusSamples(tasks,
    data[data$sys.samplerName == "weighted",],
    "mean", file.path(outdir, "samples-vs-error-weightedsampler-mean"))

tasks <- AppendAllPlotOverOrShortageVersusSamples(tasks,
    data[data$sys.samplerName == "weighted",],
    "p95", file.path(outdir, "samples-vs-error-weightedsampler-p95"))

tasks <- AppendAllPlotOverOrShortageVersusSamples(tasks,
    data[data$sys.samplerName == "uniform",],
    "mean", file.path(outdir, "samples-vs-error-uniformsampler-mean"))

tasks <- AppendAllPlotOverOrShortageVersusSamples(tasks,
    data[data$sys.samplerName == "uniform",],
    "p95", file.path(outdir, "samples-vs-error-uniformsampler-p95"))

.junk <- parallel::mccollect()
