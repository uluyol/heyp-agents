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

SumClientGoodput <- function(subset) {
    subset$Timestamp <- round(subset$Timestamp)
    aggregate(MeanBps ~ Timestamp + Group + Instance,
        FUN=sum,
        data=subset)
}

outdir <- args[1]

procdirs <- Sys.glob(file.path(outdir, "proc-indiv", "*"))
summarydir <- file.path(outdir, "proc-summary-bw-cdfs")

unlink(summarydir, recursive=TRUE)
dir.create(summarydir, recursive=TRUE)

overage <- data.frame()
goodput <- data.frame()

for (procdir in procdirs) {
    sys_name <- SYS_LONG[[gsub(".*-", "", procdir)]]

    approvals <- read.csv(file.path(procdir, "approvals.csv"), header=TRUE, stringsAsFactors=FALSE)
    client.ts <- read.csv(file.path(procdir, "ts.csv"), header=TRUE, stringsAsFactors=FALSE)
    usage.ts <- read.csv(file.path(procdir, "host-fg-usage-ts.csv"), header=TRUE, stringsAsFactors=FALSE)
    #approvals$FG <- paste0(approvals$SrcDC, "_TO_", approvals$DstDC)

    goodput.summed <- SumClientGoodput(client.ts)
    goodput <- rbind(
        goodput,
        data.frame(
            Sys=rep.int(sys_name, nrow(goodput.summed)),
            GoodputGbps=goodput.summed$MeanBps / (2^30),
            Kind=paste0(goodput.summed$Group, "/", goodput.summed$Instance)))

    a2edge.hipri <- max(0, approvals$ApprovalBps[approvals$SrcDC == "A" & approvals$DstDC == "EDGE"]) / (2^30)
    a2edge.lopri <- max(0, approvals$LOPRILimitBps[approvals$SrcDC == "A" & approvals$DstDC == "EDGE"]) / (2^30)

    b2edge.hipri <- max(0, approvals$ApprovalBps[approvals$SrcDC == "B" & approvals$DstDC == "EDGE"]) / (2^30)
    b2edge.lopri <- max(0, approvals$LOPRILimitBps[approvals$SrcDC == "B" & approvals$DstDC == "EDGE"]) / (2^30)

    usage.ts <- usage.ts[grepl("(A|B)_TO_EDGE", usage.ts$FG),]

    usage.ts$UsageGbps <- usage.ts$Usage / (2^30)
    usage.summed <- aggregate(UsageGbps ~ UnixTime + FG + QoS,
        FUN=sum,
        data=usage.ts)

    usage.summed$Overage <- rep.int(-1, nrow(usage.summed))

    mask <- usage.summed$FG == "A_TO_EDGE" & usage.summed$QoS == "HIPRI"
    usage.summed$Overage[mask] <-
        pmax(0, usage.summed$UsageGbps[mask] - a2edge.hipri)

    mask <- usage.summed$FG == "A_TO_EDGE" & usage.summed$QoS == "LOPRI"
    usage.summed$Overage[mask] <-
        pmax(0, usage.summed$UsageGbps[mask] - a2edge.lopri)

    mask <- usage.summed$FG == "B_TO_EDGE" & usage.summed$QoS == "HIPRI"
    usage.summed$Overage[mask] <-
        pmax(0, usage.summed$UsageGbps[mask] - b2edge.hipri)

    mask <- usage.summed$FG == "B_TO_EDGE" & usage.summed$QoS == "LOPRI"
    usage.summed$Overage[mask] <-
        pmax(0, usage.summed$UsageGbps[mask] - b2edge.lopri)

    overage <- rbind(
        overage,
        data.frame(
            Sys=rep.int(sys_name, nrow(usage.summed)),
            FG=usage.summed$FG,
            QoS=usage.summed$QoS,
            OverageGbps=usage.summed$Overage))
}

write.csv(goodput, file.path(summarydir, "goodput.csv"), quote=FALSE, row.names=FALSE)
write.csv(overage, file.path(summarydir, "overage.csv"), quote=FALSE, row.names=FALSE)

PlotGoodputTo <- function(subset, output) {
    subset$Sys <- factor(subset$Sys, levels=c("RateLimit", "HSC20", "QD+LimitLO", "QD", "NoLimit"))

    pdf(output, height=2.5, width=5)
    p <- ggplot(data=subset, aes(x=GoodputGbps, color=Sys, linetype=Sys)) +
        stat_ecdf(size=1, pad=FALSE) +
        xlab("Goodput (Gbps)") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 10)) +
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
        stat_ecdf(size=1, pad=FALSE) +
        xlab("Overage (Gbps)") +
        ylab("CDF across time") +
        coord_cartesian(xlim=c(0, 5)) +
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

for (kind in unique(goodput$Kind)) {
    PlotGoodputTo(goodput[goodput$Kind == kind,], file.path(summarydir, paste0("goodput-", gsub("/", "_", kind), ".pdf")))
}

for (fg in unique(overage$FG)) {
    fgsubset <- overage[overage$FG == fg,]
    for (qos in unique(fgsubset$QoS)) {
        if (sum(fgsubset$Overage[fgsubset$QoS == qos]) > 0) {
            PlotOverageTo(fgsubset[fgsubset$QoS == qos,], file.path(summarydir, paste0("overage-", fg, "-", qos, ".pdf")))
        }
    }
}
