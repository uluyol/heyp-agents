package montecarlo

import (
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/feedbacksim"
	"golang.org/x/exp/rand"
)

type perScenarioData struct {
	downgrade struct {
		IntermediateOverage        metric
		IntermediateShortage       metric
		IntermediateOverOrShortage metric
		RealizedOverage            metric
		RealizedShortage           metric
		RealizedOverOrShortage     metric
	}
	feedbackControl struct {
		ItersToConverge  metric
		NumDowngraded    metric
		NumUpgraded      metric
		NumOscillations  metric
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
			data := make([]perScenarioData, inst.Scenarios.Num())

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

				for templateID, template := range inst.Scenarios.FeedbackScenarios {
					for dfID, initDowngradeFrac := range inst.Scenarios.InitDowngradeFracs {
						for shiftID, shiftTraffic := range inst.Scenarios.ShiftTraffics {
							activeScenario := feedbacksim.NewActiveScenario(
								feedbacksim.Scenario{
									TrueDemands:       demands,
									Approval:          approval,
									MaxHostUsage:      template.MaxHostUsage,
									AggAvailableLOPRI: template.LOPRICapOverExpectedDemand * expectedDemand,
									InitDowngradeFrac: initDowngradeFrac,
									ShiftTraffic:      shiftTraffic,
									SamplerFactory:    template.SamplerFactory,
									Controller:        template.Controller,
								},
								rng,
							)
							fcResult := activeScenario.RunMultiIter(inst.NumFeedbackIters)

							id := inst.Scenarios.ID(templateID, dfID, shiftID)

							for i, io := range fcResult.IntermediateOverage {
								io /= approval
								is := fcResult.IntermediateShortage[i] / approval
								data[id].downgrade.IntermediateOverage.Record(io)
								data[id].downgrade.IntermediateShortage.Record(is)
								data[id].downgrade.IntermediateOverOrShortage.Record(io + is)
							}
							fo := fcResult.FinalOverage / approval
							fs := fcResult.FinalShortage / approval
							data[id].downgrade.RealizedOverage.Record(fo)
							data[id].downgrade.RealizedShortage.Record(fs)
							data[id].downgrade.RealizedOverOrShortage.Record(fo + fs)
							data[id].feedbackControl.ItersToConverge.Record(float64(fcResult.ItersToConverge))
							data[id].feedbackControl.NumDowngraded.Record(float64(fcResult.NumDowngraded))
							data[id].feedbackControl.NumUpgraded.Record(float64(fcResult.NumUpgraded))
							data[id].feedbackControl.NumOscillations.Record(float64(fcResult.NumOscillations))
							data[id].feedbackControl.NumQoSChanged.Record(float64(fcResult.NumDowngraded + fcResult.NumUpgraded))
							if fcResult.Converged {
								data[id].feedbackControl.NumRunsConverged++
							}
						}
					}
				}
			}

			shardData <- data
		}(shardRuns)
	}

	go func() {
		data := make([]perScenarioData, inst.Scenarios.Num())
		for shard := 0; shard < numShards; shard++ {
			t := <-shardData
			for i := range data {
				data[i].MergeFrom(&t[i])
			}
		}
		results := make([]FeedbackInstanceResult, inst.Scenarios.Num())
		hostDemandsGen := inst.HostDemands.ShortName()
		numHosts := inst.HostDemands.NumHosts()

		for id := range results {
			results[id] = FeedbackInstanceResult{
				InstanceID:                 inst.ID,
				HostDemandsGen:             hostDemandsGen,
				NumHosts:                   numHosts,
				ApprovalOverExpectedDemand: inst.ApprovalOverExpectedDemand,
				NumSamplesAtApproval:       inst.NumSamplesAtApproval,
				NumFeedbackIters:           inst.NumFeedbackIters,
				Result: ScenarioResult{
					Scenario:          inst.Scenarios.FeedbackScenarios[inst.Scenarios.TemplateID(id)],
					InitDowngradeFrac: inst.Scenarios.InitDowngradeFracs[inst.Scenarios.DowngradeFracID(id)],
					ShiftTraffic:      inst.Scenarios.ShiftTraffics[inst.Scenarios.ShiftID(id)],
					NumDataPoints:     numRuns,
					DowngradeSummary:  populateSummary(&BasicDowngradeSummary{}, &data[id].downgrade).(*BasicDowngradeSummary),
					FeedbackControlSummary: populateSummary(&FeedbackControlSummary{
						NumRunsConverged: data[id].feedbackControl.NumRunsConverged,
					}, &data[id].feedbackControl).(*FeedbackControlSummary),
				},
			}
		}

		res <- results
	}()
}
