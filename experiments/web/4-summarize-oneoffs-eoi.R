#!/usr/bin/env Rscript

library(ggplot2)

args <- commandArgs(trailingOnly=TRUE)

if (length(args) != 1) {
    stop("usage: ./4-summarize-oneoffs-eoi.R outdir")
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

PlotRetransmitsSysTo <- function(subset, output) {
    subset <- subset[subset$DC == "A" | subset$DC == "B",]
    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=RetransSegs, color=DC, linetype=DC)) +
        stat_myecdf(size=1) +
        xlab("TCP retransmits / sec") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 3e5), ylim=c(0, 1)) +
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

PlotRetransmitsDCTo <- function(subset, output) {
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD", "NoLimit"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=RetransSegs, color=Sys, linetype=Sys)) +
        stat_myecdf(size=1) +
        xlab("TCP retransmits / sec") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 3e5), ylim=c(0, 1)) +
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
        coord_cartesian(xlim=c(0, 10), ylim=c(0, 1), expand=FALSE) +
        scale_x_continuous(breaks=seq(0, 10, by=2)) +
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

summarydir <- file.path(outdir, "proc-summary-oneoffs-eoi")

unlink(summarydir, recursive=TRUE)
dir.create(summarydir, recursive=TRUE)

cfgGroups <- basename(unique(gsub("-[^-]+$", "", Sys.glob(file.path(outdir, "proc-indiv", "*")))))

for (cfgGroup in cfgGroups) {
    procdirs <- Sys.glob(file.path(outdir, "proc-indiv", paste0(cfgGroup, "*")))

    shortage <- data.frame()
    overage <- data.frame()
    goodput <- data.frame()
    retransmits.dc <- data.frame()

    for (procdir in procdirs) {
        sys_name <- SYS_LONG[[gsub(".*-", "", procdir)]]

        approvals <- read.csv(file.path(procdir, "approvals.csv"), header=TRUE, stringsAsFactors=FALSE)
        # client.ts <- read.csv(file.path(procdir, "ts.csv"), header=TRUE, stringsAsFactors=FALSE)
        # usage.ts <- read.csv(file.path(procdir, "host-fg-usage-ts.csv"), header=TRUE, stringsAsFactors=FALSE)
        # truedemand.ts <- read.csv(file.path(procdir, "true-app-demand.csv"), header=TRUE, stringsAsFactors=FALSE)
        host.ts <- read.csv(file.path(procdir, "global-host-ts.csv"), header=TRUE, stringsAsFactors=FALSE)
        startend <- read.csv(file.path(procdir, "wl-start-end.csv"), header=TRUE, stringsAsFactors=FALSE)
        #approvals$FG <- paste0(approvals$SrcDC, "_TO_", approvals$DstDC)

        startUnix <- startend$UnixTime[startend$Kind == "Start"]
        endUnix <- startend$UnixTime[startend$Kind == "End"]

        # client.ts <- Trim(client.ts, "Timestamp", startUnix, endUnix)
        # usage.ts <- Trim(usage.ts, "UnixTime", startUnix, endUnix)
        # truedemand.ts <- Trim(truedemand.ts, "UnixTime", startUnix, endUnix)
        host.ts <- Trim(host.ts, "UnixTime", startUnix, endUnix)

        # goodput.summed <- SumClientGoodput(client.ts)
        # goodput <- rbind(
        #     goodput,
        #     data.frame(
        #         Sys=rep.int(sys_name, nrow(goodput.summed)),
        #         GoodputGbps=goodput.summed$MeanBps / (2^30),
        #         Kind=paste0(goodput.summed$Group, "/", goodput.summed$Instance)))

        a2edge.hipri <- max(0, approvals$ApprovalBps[approvals$SrcDC == "A" & approvals$DstDC == "EDGE"]) / (2^30)
        a2edge.lopri <- max(0, approvals$LOPRILimitBps[approvals$SrcDC == "A" & approvals$DstDC == "EDGE"]) / (2^30)

        b2edge.hipri <- max(0, approvals$ApprovalBps[approvals$SrcDC == "B" & approvals$DstDC == "EDGE"]) / (2^30)
        b2edge.lopri <- max(0, approvals$LOPRILimitBps[approvals$SrcDC == "B" & approvals$DstDC == "EDGE"]) / (2^30)

        # usage.ts <- usage.ts[grepl("(A|B)_TO_EDGE", usage.ts$FG),]

        # usage.ts$UsageGbps <- usage.ts$Usage / (2^30)
        # usage.summed <- aggregate(UsageGbps ~ UnixTime + FG + QoS,
        #     FUN=sum,
        #     data=usage.ts)

        # usage.summed$Overage <- rep.int(-1, nrow(usage.summed))
        # usage.summed$OverageFrac <- rep.int(-1, nrow(usage.summed))

        # mask <- usage.summed$FG == "A_TO_EDGE" & usage.summed$QoS == "HIPRI"
        # usage.summed$Overage[mask] <-
        #     pmax(0, usage.summed$UsageGbps[mask] - a2edge.hipri)
        # usage.summed$OverageFrac[mask] <-
        #     usage.summed$Overage[mask] / a2edge.hipri
        # usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        # mask <- usage.summed$FG == "A_TO_EDGE" & usage.summed$QoS == "LOPRI"
        # usage.summed$Overage[mask] <-
        #     pmax(0, usage.summed$UsageGbps[mask] - a2edge.lopri)
        # usage.summed$OverageFrac[mask] <-
        #     usage.summed$Overage[mask] / a2edge.lopri
        # usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        # mask <- usage.summed$FG == "B_TO_EDGE" & usage.summed$QoS == "HIPRI"
        # usage.summed$Overage[mask] <-
        #     pmax(0, usage.summed$UsageGbps[mask] - b2edge.hipri)
        # usage.summed$OverageFrac[mask] <-
        #     usage.summed$Overage[mask] / b2edge.hipri
        # usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        # mask <- usage.summed$FG == "B_TO_EDGE" & usage.summed$QoS == "LOPRI"
        # usage.summed$Overage[mask] <-
        #     pmax(0, usage.summed$UsageGbps[mask] - b2edge.lopri)
        # usage.summed$OverageFrac[mask] <-
        #     usage.summed$Overage[mask] / b2edge.lopri
        # usage.summed$OverageFrac[mask & usage.summed$Overage == 0] <- 0

        # overage <- rbind(
        #     overage,
        #     data.frame(
        #         Sys=rep.int(sys_name, nrow(usage.summed)),
        #         FG=usage.summed$FG,
        #         QoS=usage.summed$QoS,
        #         OverageGbps=usage.summed$Overage,
        #         OverageFrac=usage.summed$OverageFrac))

        # truedemand.ts$TrueDemandGbps <- truedemand.ts$Demand / (2^30)

        # truedemand.a2edge <- truedemand.ts[truedemand.ts$FG == "A_TO_EDGE",]
        # truedemand.b2edge <- truedemand.ts[truedemand.ts$FG == "B_TO_EDGE",]

        # admitted.want <- rbind(
        #     data.frame(
        #         UnixTime=truedemand.a2edge$UnixTime,
        #         FG=truedemand.a2edge$FG,
        #         QoS=rep.int("HIPRI", nrow(truedemand.a2edge)),
        #         WantGbps=pmin(truedemand.a2edge$TrueDemandGbps, a2edge.hipri)),
        #     data.frame(
        #         UnixTime=truedemand.a2edge$UnixTime,
        #         FG=truedemand.a2edge$FG,
        #         QoS=rep.int("LOPRI", nrow(truedemand.a2edge)),
        #         WantGbps=pmin(pmax(0, truedemand.a2edge$TrueDemandGbps - a2edge.hipri), a2edge.lopri)),
        #     data.frame(
        #         UnixTime=truedemand.b2edge$UnixTime,
        #         FG=truedemand.b2edge$FG,
        #         QoS=rep.int("HIPRI", nrow(truedemand.b2edge)),
        #         WantGbps=pmin(truedemand.b2edge$TrueDemandGbps, b2edge.hipri)),
        #     data.frame(
        #         UnixTime=truedemand.b2edge$UnixTime,
        #         FG=truedemand.b2edge$FG,
        #         QoS=rep.int("LOPRI", nrow(truedemand.b2edge)),
        #         WantGbps=pmin(pmax(0, truedemand.b2edge$TrueDemandGbps - b2edge.hipri), a2edge.lopri)))

        # shortage.this <- merge(usage.summed, admitted.want, by=c("UnixTime", "FG", "QoS"))
        # shortage.this$ShortageGbps <- pmax(0, shortage.this$WantGbps - shortage.this$UsageGbps)
        # shortage.this$ShortageFrac <- shortage.this$ShortageGbps / shortage.this$WantGbps
        # shortage.this$ShortageFrac[shortage.this$WantGbps == 0] <- 0

        # shortage.this$Sys <- rep.int(sys_name, nrow(shortage.this))
        # shortage.this <- shortage.this[,c("Sys", "FG", "QoS", "ShortageGbps", "ShortageFrac")]
        # shortage <- rbind(shortage, shortage.this)

        host.ts$DC <- rep.int("Unknown", nrow(host.ts))
        host.ts$DC[grepl("^n(2|3)$", host.ts$Node)] <- "EDGE"
        host.ts$DC[grepl("^n(4|5|6|7|8|9|10|11|12)$", host.ts$Node)] <- "A"
        host.ts$DC[grepl("^n(13|14)$", host.ts$Node)] <- "B"
        host.ts$DC[grepl("^n(15|16)$", host.ts$Node)] <- "CLIENT"

        retransmits.dc.this <- aggregate(RetransSegs ~ UnixTime + DC, data=host.ts, FUN=sum)
        retransmits.dc.this$Sys <- rep.int(sys_name, nrow(retransmits.dc.this))
        retransmits.dc <- rbind(retransmits.dc, retransmits.dc.this)
    }

    # write.csv(goodput, file.path(summarydir, paste0(cfgGroup, "-goodput.csv")), quote=FALSE, row.names=FALSE)
    # write.csv(overage, file.path(summarydir, paste0(cfgGroup, "-overage.csv")), quote=FALSE, row.names=FALSE)
    # write.csv(shortage, file.path(summarydir, paste0(cfgGroup, "-shortage.csv")), quote=FALSE, row.names=FALSE)
    write.csv(retransmits.dc, file.path(summarydir, paste0(cfgGroup, "-retransmits_dc.csv")), quote=FALSE, row.names=FALSE)

    # for (kind in unique(goodput$Kind)) {
    #     PlotGoodputTo(goodput[goodput$Kind == kind,], file.path(summarydir, paste0(cfgGroup, "-goodput-", gsub("/", "_", kind), ".pdf")))
    # }

    for (dc in unique(retransmits.dc$DC)) {
        PlotRetransmitsDCTo(
            retransmits.dc[retransmits.dc$DC == dc,],
            file.path(summarydir, paste0(cfgGroup, "-retransmits-DC-", dc, ".pdf")))
    }

    for (sys in unique(retransmits.dc$Sys)) {
        PlotRetransmitsSysTo(
            retransmits.dc[retransmits.dc$Sys == sys,],
            file.path(summarydir, paste0(cfgGroup, "-retransmits-SYS-", sys, ".pdf")))
    }

    for (fg in unique(overage$FG)) {
        fgsubset <- overage[overage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (sum(fgsubset$OverageGbps[fgsubset$QoS == qos]) > 0) {
                PlotOverageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-overage-", fg, "-", qos, ".pdf")))
            }
        }
    }

    for (fg in unique(shortage$FG)) {
        fgsubset <- shortage[shortage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (sum(fgsubset$ShortageGbps[fgsubset$QoS == qos]) > 0) {
                PlotShortageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-shortage-", fg, "-", qos, ".pdf")))
            }
        }
    }

    for (fg in unique(overage$FG)) {
        fgsubset <- overage[overage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (sum(fgsubset$OverageGbps[fgsubset$QoS == qos]) > 0) {
                PlotFracOverageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-fracoverage-", fg, "-", qos, ".pdf")))
            }
        }
    }

    for (fg in unique(shortage$FG)) {
        fgsubset <- shortage[shortage$FG == fg,]
        for (qos in unique(fgsubset$QoS)) {
            if (sum(fgsubset$ShortageGbps[fgsubset$QoS == qos]) > 0) {
                PlotFracShortageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0(cfgGroup, "-fracshortage-", fg, "-", qos, ".pdf")))
            }
        }
    }
}
