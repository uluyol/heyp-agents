#!/usr/bin/env Rscript

library(ggplot2)

args <- commandArgs(trailingOnly=TRUE)

if (length(args) != 1) {
    stop("usage: ./4-summarize-bw-cdfs.R outdir")
}

SYS_LONG <- new.env(hash=TRUE)
SYS_LONG[["hsc"]] <- "HSC20"
SYS_LONG[["nl"]] <- "NoLimit"
SYS_LONG[["qd"]] <- "QD"
SYS_LONG[["qdlrl"]] <- "QD+LimitLO"
SYS_LONG[["rl"]] <- "RateLimit"

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

SumClientGoodput <- function(subset) {
    subset$Timestamp <- round(subset$Timestamp)
    aggregate(MeanBps ~ Timestamp + Group + Instance,
        FUN=sum,
        data=subset)
}

PlotRetransmitsAggTo <- function(subset, output) {
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD", "NoLimit"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=RetransSegs, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("TCP retransmits / sec") +
        ylab("CDF across time") +
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

PlotEgressMinuxIngressAggTo <- function(subset, output) {
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD", "NoLimit"))

    subset$X <- (subset$EgressBps - subset$IngressBps) / (2 ^ 30)

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=X, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("Egress - Ingress (Gbps)") +
        ylab("CDF across time") +
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

PlotGoodputTo <- function(subset, output) {
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD", "NoLimit"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=GoodputGbps, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("Goodput (Gbps)") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 14), ylim=c(0, 1), expand=FALSE) +
        scale_x_continuous(breaks=seq(0, 14, by=2)) +
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

PlotQoSChurnTo <- function(subset, output) {
    subset <- subset[subset$Sys != "NoLimit",]
    subset <- subset[subset$Sys != "RateLimit",]
    subset$Sys <- factor(subset$Sys, levels=c("HSC20", "QD+LimitLO", "QD"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=FracChanged, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("Fraction of hosts that changed QoS") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 1), ylim=c(0, 1), expand=FALSE) +
        scale_x_continuous(breaks=seq(0, 1, by=0.2)) +
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

PlotOverageTo <- function(subset, output) {
    subset <- subset[subset$Sys != "NoLimit",]
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=OverageGbps, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("Overage (Gbps)") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 5), ylim=c(0, 1), expand=FALSE) +
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

PlotShortageTo <- function(subset, output) {
    subset <- subset[subset$Sys != "NoLimit",]
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=ShortageGbps, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("Shortage (Gbps)") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 5), ylim=c(0, 1), expand=FALSE) +
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

PlotFracOverageTo <- function(subset, output) {
    subset <- subset[subset$Sys != "NoLimit",]
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=OverageFrac, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("Overage / limit") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(-0.05, 1.05), ylim=c(-0.05, 1.05), expand=FALSE) +
        scale_x_continuous(breaks=seq(0, 1, by=0.2)) +
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

PlotFracShortageTo <- function(subset, output) {
    subset <- subset[subset$Sys != "NoLimit",]
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=ShortageFrac, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("Shortage / min(demand, limit)") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(-0.05, 1.05), ylim=c(-0.05, 1.05), expand=FALSE) +
        scale_x_continuous(breaks=seq(0, 1, by=0.2)) +
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

START_END_TRIM_SEC = 15

Trim <- function(df, timecol, startUnix, endUnix) {
    df[df[[timecol]] >= (startUnix + START_END_TRIM_SEC) &
       df[[timecol]] <= (endUnix - START_END_TRIM_SEC),]
}

outdir <- args[1]

summarydir <- file.path(outdir, "proc-summary-bw-cdfs")

unlink(summarydir, recursive=TRUE)
dir.create(summarydir, recursive=TRUE)

cfgGroups <- basename(unique(gsub("-[^-]+$", "", Sys.glob(file.path(outdir, "proc-indiv", "*")))))

for (cfgGroup in cfgGroups) {
    procdirs <- Sys.glob(file.path(outdir, "proc-indiv", paste0(cfgGroup, "*")))

    shortage <- data.frame()
    overage <- data.frame()
    goodput <- data.frame()
    retransmits.agg <- data.frame()
    qos.churn <- data.frame()

    for (procdir in procdirs) {
        sys_name <- SYS_LONG[[gsub(".*-", "", procdir)]]

        approvals <- read.csv(file.path(procdir, "approvals.csv"), header=TRUE, stringsAsFactors=FALSE)
        client.ts <- read.csv(file.path(procdir, "ts.csv"), header=TRUE, stringsAsFactors=FALSE)
        usage.ts <- read.csv(file.path(procdir, "host-fg-usage-ts.csv"), header=TRUE, stringsAsFactors=FALSE)
        truedemand.ts <- read.csv(file.path(procdir, "true-app-demand.csv"), header=TRUE, stringsAsFactors=FALSE)
        host.ts <- read.csv(file.path(procdir, "global-host-ts.csv"), header=TRUE, stringsAsFactors=FALSE)
        qos.retained.ts <- read.csv(file.path(procdir, "cluster-alloc-qos-retained.csv"), header=TRUE, stringsAsFactors=FALSE)
        startend <- read.csv(file.path(procdir, "wl-start-end.csv"), header=TRUE, stringsAsFactors=FALSE)
        #approvals$FG <- paste0(approvals$SrcDC, "_TO_", approvals$DstDC)

        startUnix <- startend$UnixTime[startend$Kind == "Start"]
        endUnix <- startend$UnixTime[startend$Kind == "End"]

        client.ts <- Trim(client.ts, "Timestamp", startUnix, endUnix)
        usage.ts <- Trim(usage.ts, "UnixTime", startUnix, endUnix)
        truedemand.ts <- Trim(truedemand.ts, "UnixTime", startUnix, endUnix)
        host.ts <- Trim(host.ts, "UnixTime", startUnix, endUnix)
        qos.retained.ts <- Trim(qos.retained.ts, "UnixTime", startUnix, endUnix)

        goodput.summed <- SumClientGoodput(client.ts)
        goodput <- rbind(
            goodput,
            data.frame(
                Sys=rep.int(sys_name, nrow(goodput.summed)),
                GoodputGbps=goodput.summed$MeanBps / (2^30),
                Kind=paste0(goodput.summed$Group, "/", goodput.summed$Instance)))

        aa2edge.hipri <- max(0, approvals$ApprovalBps[approvals$SrcDC == "AA" & approvals$DstDC == "EDGE"]) / (2^30)
        aa2edge.lopri <- max(0, approvals$LOPRILimitBps[approvals$SrcDC == "AA" & approvals$DstDC == "EDGE"]) / (2^30)

        wa2edge.hipri <- max(0, approvals$ApprovalBps[approvals$SrcDC == "WA" & approvals$DstDC == "EDGE"]) / (2^30)
        wa2edge.lopri <- max(0, approvals$LOPRILimitBps[approvals$SrcDC == "WA" & approvals$DstDC == "EDGE"]) / (2^30)

        usage.ts <- usage.ts[grepl("(AA|WA)_TO_EDGE", usage.ts$FG),]

        usage.ts$UsageGbps <- usage.ts$Usage / (2^30)
        usage.summed <- aggregate(UsageGbps ~ UnixTime + FG + QoS,
            FUN=sum,
            data=usage.ts)

        usage.summed$Overage <- rep.int(-1, nrow(usage.summed))
        usage.summed$OverageFrac <- rep.int(-1, nrow(usage.summed))

        mask <- usage.summed$FG == "AA_TO_EDGE" & usage.summed$QoS == "HIPRI"
        usage.summed$Overage[mask] <-
            pmax(0, usage.summed$UsageGbps[mask] - aa2edge.hipri)
        usage.summed$OverageFrac[mask] <-
            usage.summed$Overage[mask] / aa2edge.hipri
        usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        mask <- usage.summed$FG == "AA_TO_EDGE" & usage.summed$QoS == "LOPRI"
        usage.summed$Overage[mask] <-
            pmax(0, usage.summed$UsageGbps[mask] - aa2edge.lopri)
        usage.summed$OverageFrac[mask] <-
            usage.summed$Overage[mask] / aa2edge.lopri
        usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        mask <- usage.summed$FG == "WA_TO_EDGE" & usage.summed$QoS == "HIPRI"
        usage.summed$Overage[mask] <-
            pmax(0, usage.summed$UsageGbps[mask] - wa2edge.hipri)
        usage.summed$OverageFrac[mask] <-
            usage.summed$Overage[mask] / wa2edge.hipri
        usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        mask <- usage.summed$FG == "WA_TO_EDGE" & usage.summed$QoS == "LOPRI"
        usage.summed$Overage[mask] <-
            pmax(0, usage.summed$UsageGbps[mask] - wa2edge.lopri)
        usage.summed$OverageFrac[mask] <-
            usage.summed$Overage[mask] / wa2edge.lopri
        usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        overage <- rbind(
            overage,
            data.frame(
                Sys=rep.int(sys_name, nrow(usage.summed)),
                FG=usage.summed$FG,
                QoS=usage.summed$QoS,
                OverageGbps=usage.summed$Overage,
                OverageFrac=usage.summed$OverageFrac))

        truedemand.ts$TrueDemandGbps <- truedemand.ts$Demand / (2^30)

        truedemand.aa2edge <- truedemand.ts[truedemand.ts$FG == "AA_TO_EDGE",]
        truedemand.wa2edge <- truedemand.ts[truedemand.ts$FG == "WA_TO_EDGE",]

        admitted.want <- rbind(
            data.frame(
                UnixTime=truedemand.aa2edge$UnixTime,
                FG=truedemand.aa2edge$FG,
                QoS=rep.int("HIPRI", nrow(truedemand.aa2edge)),
                WantGbps=pmin(truedemand.aa2edge$TrueDemandGbps, aa2edge.hipri)),
            data.frame(
                UnixTime=truedemand.aa2edge$UnixTime,
                FG=truedemand.aa2edge$FG,
                QoS=rep.int("LOPRI", nrow(truedemand.aa2edge)),
                WantGbps=pmin(pmax(0, truedemand.aa2edge$TrueDemandGbps - aa2edge.hipri), aa2edge.lopri)),
            data.frame(
                UnixTime=truedemand.wa2edge$UnixTime,
                FG=truedemand.wa2edge$FG,
                QoS=rep.int("HIPRI", nrow(truedemand.wa2edge)),
                WantGbps=pmin(truedemand.wa2edge$TrueDemandGbps, wa2edge.hipri)),
            data.frame(
                UnixTime=truedemand.wa2edge$UnixTime,
                FG=truedemand.wa2edge$FG,
                QoS=rep.int("LOPRI", nrow(truedemand.wa2edge)),
                WantGbps=pmin(pmax(0, truedemand.wa2edge$TrueDemandGbps - wa2edge.hipri), aa2edge.lopri)))

        shortage.this <- merge(usage.summed, admitted.want, by=c("UnixTime", "FG", "QoS"))
        shortage.this$ShortageGbps <- pmax(0, shortage.this$WantGbps - shortage.this$UsageGbps)
        shortage.this$ShortageFrac <- shortage.this$ShortageGbps / shortage.this$WantGbps
        shortage.this$ShortageFrac[shortage.this$WantGbps == 0] <- 0

        shortage.this$Sys <- rep.int(sys_name, nrow(shortage.this))
        shortage.this <- shortage.this[,c("Sys", "FG", "QoS", "ShortageGbps", "ShortageFrac")]
        shortage <- rbind(shortage, shortage.this)

        retransmits.agg.this <- aggregate(cbind(RetransSegs, IngressBps, EgressBps) ~ UnixTime, data=host.ts, FUN=sum)
        retransmits.agg.this$Sys <- rep.int(sys_name, nrow(retransmits.agg.this))
        retransmits.agg <- rbind(retransmits.agg, retransmits.agg.this)

        qos.retained.ts <- qos.retained.ts[qos.retained.ts$FG == "AA_TO_EDGE",]
        qos.churn <- rbind(qos.churn,
            rbind(
                data.frame(
                    UnixTime=qos.retained.ts$UnixTime,
                    FracChanged=1-qos.retained.ts$FracHIPRIRetained,
                    Kind=rep.int("HI to LOPRI", nrow(qos.retained.ts)),
                    Sys=rep.int(sys_name, nrow(qos.retained.ts))),
                data.frame(
                    UnixTime=qos.retained.ts$UnixTime,
                    FracChanged=1-qos.retained.ts$FracLOPRIRetained,
                    Kind=rep.int("LO to HIPRI", nrow(qos.retained.ts)),
                    Sys=rep.int(sys_name, nrow(qos.retained.ts)))))
    }

    write.csv(goodput, file.path(summarydir, paste0(cfgGroup, "-goodput.csv")), quote=FALSE, row.names=FALSE)
    write.csv(overage, file.path(summarydir, paste0(cfgGroup, "-overage.csv")), quote=FALSE, row.names=FALSE)
    write.csv(shortage, file.path(summarydir, paste0(cfgGroup, "-shortage.csv")), quote=FALSE, row.names=FALSE)
    write.csv(retransmits.agg, file.path(summarydir, paste0(cfgGroup, "-retransmits_agg.csv")), quote=FALSE, row.names=FALSE)
    write.csv(qos.churn, file.path(summarydir, paste0(cfgGroup, "-qos_churn.csv")), quote=FALSE, row.names=FALSE)

    for (kind in unique(goodput$Kind)) {
        PlotGoodputTo(goodput[goodput$Kind == kind,], file.path(summarydir, paste0(cfgGroup, "-goodput-", gsub("/", "_", kind), ".pdf")))
    }

    PlotRetransmitsAggTo(retransmits.agg, file.path(summarydir, paste0(cfgGroup, "-retransmits_agg.pdf")))
    PlotEgressMinuxIngressAggTo(retransmits.agg, file.path(summarydir, paste0(cfgGroup, "-egress_minus_ingress_agg.pdf")))

    for (fg in unique(overage$FG)) {
        fgsubset <- overage[overage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (qos != "LOPRI" && sum(fgsubset$OverageGbps[fgsubset$QoS == qos]) > 0) {
                PlotOverageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-bw-overage-", fg, "-", qos, ".pdf")))
            }
        }
    }

    for (fg in unique(shortage$FG)) {
        fgsubset <- shortage[shortage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (qos != "LOPRI" && sum(fgsubset$ShortageGbps[fgsubset$QoS == qos]) > 0) {
                PlotShortageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-bw-shortage-", fg, "-", qos, ".pdf")))
            }
        }
    }

    for (fg in unique(overage$FG)) {
        fgsubset <- overage[overage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (qos != "LOPRI" && sum(fgsubset$OverageGbps[fgsubset$QoS == qos]) > 0) {
                PlotFracOverageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-fracoverage-", fg, "-", qos, ".pdf")))
            }
        }
    }

    for (fg in unique(shortage$FG)) {
        fgsubset <- shortage[shortage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (qos != "LOPRI" && sum(fgsubset$ShortageGbps[fgsubset$QoS == qos]) > 0) {
                PlotFracShortageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-fracshortage-", fg, "-", qos, ".pdf")))
            }
        }
    }

    for (kind in unique(qos.churn$Kind)) {
        PlotQoSChurnTo(qos.churn[qos.churn$Kind == kind,], file.path(summarydir, paste0(cfgGroup, "-qos_churn-", gsub(" ", "_", kind), ".pdf")))
    }
}
