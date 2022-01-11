package montecarlo

import (
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/feedbacksim"
	"golang.org/x/exp/rand"
)

type perScenarioData struct {
	downgrade struct {
		RealizedOverage        metric
		RealizedShortage       metric
		RealizedOverOrShortage metric
	}
	feedbackControl struct {
		ItersToConverge  metric
		NumDowngraded    metric
		NumUpgraded      metric
		NumQoSChanged    metric
		NumRunsConverged int
	}
}

func (r *perScenarioData) MergeFrom(o *perScenarioData) {
	mergeMetricsInto(&o.downgrade, &r.downgrade)
	mergeMetricsInto(&o.feedbackControl, &r.feedbackControl)
}

// EvalFeedbackInstance is like EvalInstance but for feedback control.
func EvalFeedbackInstance(inst FeedbackInstance, numRuns int, sem chan Token, res chan<- []FeedbackInstanceResult) {
	expectedDemand := float64(inst.HostDemands.NumHosts()) * inst.HostDemands.DistMean()
	approval := inst.ApprovalOverExpectedDemand * expectedDemand

	shardSize := shardSize(inst.HostDemands.NumHosts())
	shardData := make(chan []perScenarioData, (numRuns/shardSize)+1)
	numShards := 0
	for shardStart := 0; shardStart < numRuns; shardStart += shardSize {
		numShards++
		sem <- Token{}
		shardRuns := shardSize
		if t := numRuns - shardStart; t < shardRuns {
			shardRuns = t
		}
		go func(shardRuns int) {
			defer func() {
				<-sem
			}()
			data := make([]perScenarioData, len(inst.FeedbackScenarios))

			var (
				// This simulation is non-deterministic, should be fine
				rng     = rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
				demands []float64
			)

			for run := 0; run < shardRuns; run++ {
				demands = inst.HostDemands.GenDist(rng, demands)

				var exactDemand float64
				for _, v := range demands {
					exactDemand += v
				}

				// exactApprovedUsage := math.Min(approval, exactUsage)
				for scenarioID, scenarioTmpl := range inst.FeedbackScenarios {
					activeScenario := feedbacksim.NewActiveScenario(
						feedbacksim.Scenario{
							TrueDemands:       demands,
							Approval:          approval,
							MaxHostUsage:      scenarioTmpl.MaxHostUsage,
							AggAvailableLOPRI: scenarioTmpl.LOPRICapOverExpectedDemand * expectedDemand,
							ShiftTraffic:      scenarioTmpl.ShiftTraffic,
							SamplerFactory:    scenarioTmpl.SamplerFactory,
							Controller:        scenarioTmpl.Controller,
						},
						rng,
					)
					fcResult := activeScenario.RunMultiIter(inst.NumFeedbackIters)

					// approxUsage := estimateUsage(rng, sampler, usages, sampleTracker)

					// approxRealizedDowngradeFrac := downgradeFracAfterHostSel(
					// 	hostSel.NewMatcher(approxDowngradeFrac, sortedSampledUsages),
					// 	usages, exactUsage)

					// realizedError := normByExpected((1-approxRealizedDowngradeFrac)*exactUsage-exactApprovedUsage, exactApprovedUsage)
					// realizedOverage := math.Max(0, realizedError)
					// realizedShortage := -math.Min(0, realizedError)

					// data[scenarioID].downgrade.RealizedOverage.Record(realizedOverage)
					// data[scenarioID].downgrade.RealizedShortage.Record(realizedShortage)
					// data[scenarioID].downgrade.RealizedOverOrShortage.Record(math.Abs(realizedError))
					data[scenarioID].feedbackControl.ItersToConverge.Record(float64(fcResult.ItersToConverge))
					data[scenarioID].feedbackControl.NumDowngraded.Record(float64(fcResult.NumDowngraded))
					data[scenarioID].feedbackControl.NumUpgraded.Record(float64(fcResult.NumUpgraded))
					data[scenarioID].feedbackControl.NumQoSChanged.Record(float64(fcResult.NumDowngraded + fcResult.NumUpgraded))
					if fcResult.Converged {
						data[scenarioID].feedbackControl.NumRunsConverged++
					}
				}
			}

			shardData <- data
		}(shardRuns)
	}

	go func() {
		data := make([]perScenarioData, len(inst.FeedbackScenarios))
		for shard := 0; shard < numShards; shard++ {
			t := <-shardData
			for i := range data {
				data[i].MergeFrom(&t[i])
			}
		}
		results := make([]FeedbackInstanceResult, len(inst.FeedbackScenarios))
		hostDemandsGen := inst.HostDemands.ShortName()
		numHosts := inst.HostDemands.NumHosts()

		for scenarioID := range results {
			results[scenarioID] = FeedbackInstanceResult{
				InstanceID:                 inst.ID,
				HostDemandsGen:             hostDemandsGen,
				NumHosts:                   numHosts,
				ApprovalOverExpectedDemand: inst.ApprovalOverExpectedDemand,
				NumSamplesAtApproval:       inst.NumSamplesAtApproval,
				NumFeedbackIters:           inst.NumFeedbackIters,
				Result: ScenarioResult{
					Scenario:         inst.FeedbackScenarios[scenarioID],
					NumDataPoints:    numRuns,
					DowngradeSummary: populateSummary(&BasicDowngradeSummary{}, &data[scenarioID].downgrade).(*BasicDowngradeSummary),
					FeedbackControlSummary: populateSummary(&FeedbackControlSummary{
						NumRunsConverged: data[scenarioID].feedbackControl.NumRunsConverged,
					}, &data[scenarioID].feedbackControl).(*FeedbackControlSummary),
				},
			}
		}

		res <- results
	}()
}
