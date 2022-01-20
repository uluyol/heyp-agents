#!/usr/bin/env Rscript

library(ggplot2)
library(parallel)

args <- commandArgs(trailingOnly=TRUE)

if (length(args) != 1) {
    stop("usage: ./4-summarize-latency.R outdir")
}

SYS_LONG <- new.env(hash=TRUE)
SYS_LONG[["hsc"]] <- "HSC20"
SYS_LONG[["nl"]] <- "NoLimit"
SYS_LONG[["nl_light"]] <- "NoLimit-LightAA"
SYS_LONG[["qd"]] <- "QD"
SYS_LONG[["qd_fc"]] <- "QD+FC"
SYS_LONG[["qdhrl"]] <- "QD+LimitHI"
SYS_LONG[["qdlrl"]] <- "QD+LimitLO"
SYS_LONG[["qdlrl_fc"]] <- "QD+FC+LimitLO"
SYS_LONG[["qdlrlj"]] <- "QDJob+LimitLO"
SYS_LONG[["rl"]] <- "RateLimit"
SYS_LONG[["rl_light"]] <- "RateLimit-LightAA"
SYS_LONG[["flipflop"]] <- "MixedFlipFlop"
SYS_LONG[["stableqos"]] <- "MixedStable"
SYS_LONG[["flipflop_nl"]] <- "MixedFlipFlop-NL"
SYS_LONG[["stableqos_nl"]] <- "MixedStable-NL"
SYS_LONG[["flipflop_rl"]] <- "MixedFlipFlop-RLTight"
SYS_LONG[["stableqos_rl"]] <- "MixedStable-RLTight"
SYS_LONG[["flipflop_oversub"]] <- "MixedFlipFlop-RL"
SYS_LONG[["stableqos_oversub"]] <- "MixedStable-RL"
SYS_LONG[["hipri"]] <- "AllHIPRI"
SYS_LONG[["lopri"]] <- "AllLOPRI"

PlotLatencyCumCountsTo <- function(ymax, subset, output) {
    if (nrow(subset) > 0) {
        # subset$Sys <- factor(subset$Sys, levels=c("NoLimit", "AllHIPRI", "MixedStable", "MixedFlipFlop", "AllLOPRI"))
        subset$Y <- subset$CumNumSamples

        pdf(output, height=2.5, width=5)
        p <- ggplot(data=subset, aes(x=LatencyNanos / 1e6, y=Y, color=Sys, linetype=Sys)) +
            geom_step(size=1) +
            xlab("Latency (ms)") +
            ylab("# of requests with latency < X") +
            coord_cartesian(xlim=c(0, 350), ylim=c(0, ymax), expand=FALSE) +
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
}

PlotLatencyCDFTo <- function(subset, output) {
    if (nrow(subset) > 0) {
        # subset$Sys <- factor(subset$Sys, levels=c("NoLimit", "AllHIPRI", "MixedStable", "MixedFlipFlop", "AllLOPRI"))
        subset$Y <- subset$Percentile / 100

        pdf(output, height=2.5, width=5)
        p <- ggplot(data=subset, aes(x=LatencyNanos / 1e6, y=Y, color=Sys, linetype=Sys)) +
            geom_step(size=1) +
            xlab("Latency (ms)") +
            ylab("CDF across requests") +
            coord_cartesian(xlim=c(0, 350), ylim=c(0, 1), expand=FALSE) +
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
}

PlotLatencyCCDFTo <- function(subset, output) {
    if (nrow(subset) > 0) {
        # subset$Sys <- factor(subset$Sys, levels=c("NoLimit", "AllHIPRI", "MixedStable", "MixedFlipFlop", "AllLOPRI"))
        subset$Y <- 1 - (subset$Percentile / 100)
        subset <- subset[subset$Y > 0,]

        pdf(output, height=2.5, width=5)
        p <- ggplot(data=subset, aes(x=LatencyNanos / 1e6, y=Y, color=Sys, linetype=Sys)) +
            geom_step(size=1) +
            xlab("Latency (ms)") +
            ylab("CCDF across requests") +
            coord_cartesian(xlim=c(0, 450), ylim=c(0.0005, 1), expand=FALSE) +
            scale_y_log10(breaks=c(0.001, 0.01, 0.1, 0.5, 1), labels=c("99.9%", "99%", "90%", "50%", "0%")) +
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
}

START_END_TRIM_SEC = 15

Trim <- function(df, timecol, startUnix, endUnix) {
    df[df[[timecol]] >= (startUnix + START_END_TRIM_SEC) &
       df[[timecol]] <= (endUnix - START_END_TRIM_SEC),]
}

outdir <- args[1]

summarydir <- file.path(outdir, "proc-summary-latency")

unlink(summarydir, recursive=TRUE)
dir.create(summarydir, recursive=TRUE)

cfgGroups <- basename(unique(gsub("-[^-]+$", "", Sys.glob(file.path(outdir, "proc-indiv", "*")))))

ProcCfgGroup <- function(cfgGroup) {
    procdirs <- Sys.glob(file.path(outdir, "proc-indiv", paste0(cfgGroup, "*")))

    latency <- data.frame()

    for (procdir in procdirs) {
        sys_name <- SYS_LONG[[gsub(".*-", "", procdir)]]

        latency.this <- read.csv(file.path(procdir, "cdf-per-instance.csv"), header=TRUE, stringsAsFactors=FALSE)
        latency.this <- latency.this[latency.this$LatencyKind == "net", c("Group", "Instance", "Percentile", "CumNumSamples", "LatencyNanos")]
        latency.this$Sys <- rep.int(sys_name, nrow(latency.this))

        latency <- rbind(latency, latency.this)

        # startend <- read.csv(file.path(procdir, "wl-start-end.csv"), header=TRUE, stringsAsFactors=FALSE)
        # startUnix <- startend$UnixTime[startend$Kind == "Start"]
        # endUnix <- startend$UnixTime[startend$Kind == "End"]

    }

    write.csv(latency, file.path(summarydir, paste0(cfgGroup, "-latency.csv")), quote=FALSE, row.names=FALSE)

    for (group in unique(latency$Group)) {
        for (inst in unique(latency$Instance)) {
            PlotLatencyCumCountsTo(
                max(latency$CumNumSamples),
                latency[latency$Group == group & latency$Instance == inst,],
                file.path(summarydir, paste0(cfgGroup, "-counts-", group, "_", inst, ".pdf")))
            PlotLatencyCDFTo(
                latency[latency$Group == group & latency$Instance == inst,],
                file.path(summarydir, paste0(cfgGroup, "-cdf-", group, "_", inst, ".pdf")))
            PlotLatencyCCDFTo(
                latency[latency$Group == group & latency$Instance == inst,],
                file.path(summarydir, paste0(cfgGroup, "-ccdf-", group, "_", inst, ".pdf")))
        }
    }
}

numCores <- parallel::detectCores()
.junk <- parallel::mclapply(cfgGroups, ProcCfgGroup, mc.cores = numCores)
