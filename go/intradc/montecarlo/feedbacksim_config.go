package montecarlo

import (
	"github.com/uluyol/heyp-agents/go/intradc/dists"
	"github.com/uluyol/heyp-agents/go/intradc/feedbacksim"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
)

type FeedbackScenarioTemplate struct {
	MaxHostUsage               float64                             `json:"maxHostUsage"`
	LOPRICapOverExpectedDemand float64                             `json:"lopriCapOverExpectedDemand"`
	SamplerFactory             sampling.SamplerFactory             `json:"samplerFactory"`
	Controller                 feedbacksim.DowngradeFracController `json:"controller"`
}

type FeedbackConfig struct {
	HostDemands                []dists.ConfigDistGen      `json:"hostDemands"`
	NumHosts                   []int                      `json:"numHosts"`
	ApprovalOverExpectedDemand []float64                  `json:"approvalOverExpectedDemand"`
	NumSamplesAtApproval       []int                      `json:"numSamplesAtApproval"`
	NumFeedbackIters           int                        `json:"numFeedbackIters"`
	FeedbackScenarios          []FeedbackScenarioTemplate `json:"feedbackScenarios"`
}

func (c FeedbackConfig) validateData() validateData {
	return validateData{
		hostVols:             c.HostDemands,
		hostVolsName:         "HostDemands",
		numHosts:             c.NumHosts,
		aoe:                  c.ApprovalOverExpectedDemand,
		aoeName:              "ApprovalOverExpectedDemand",
		numSamplesAtApproval: c.NumSamplesAtApproval,
	}
}

var _ EnumerableConfig = FeedbackConfig{}

func (c *FeedbackConfig) Enumerate() []FeedbackInstance {
	var instances []FeedbackInstance
	for _, dg := range c.HostDemands {
		for _, numHosts := range c.NumHosts {
			distGen := dg.Gen.WithNumHosts(numHosts)
			for _, aod := range c.ApprovalOverExpectedDemand {
				for _, numSamples := range c.NumSamplesAtApproval {
					id := ID(len(instances))
					instances = append(instances, FeedbackInstance{
						ID:                         id,
						HostDemands:                distGen,
						ApprovalOverExpectedDemand: aod,
						NumSamplesAtApproval:       numSamples,
						NumFeedbackIters:           c.NumFeedbackIters,
						Scenarios: MultiScenario{
							FeedbackScenarios:  c.FeedbackScenarios,
							InitDowngradeFracs: []float64{0, 1},
							ShiftTraffics:      []bool{false, true},
						},
					})
				}
			}
		}
	}
	return instances
}

type FeedbackInstance struct {
	ID                         ID
	HostDemands                dists.DistGen
	ApprovalOverExpectedDemand float64
	NumSamplesAtApproval       int
	NumFeedbackIters           int
	Scenarios                  MultiScenario
}

type MultiScenario struct {
	FeedbackScenarios  []FeedbackScenarioTemplate
	InitDowngradeFracs []float64
	ShiftTraffics      []bool
}

func (s *MultiScenario) Num() int {
	return len(s.FeedbackScenarios) * len(s.InitDowngradeFracs) * len(s.ShiftTraffics)
}

func (s *MultiScenario) ID(templateID, downgradeFracID, shiftID int) int {
	return (templateID*len(s.InitDowngradeFracs)+downgradeFracID)*len(s.ShiftTraffics) + shiftID
}

func (s *MultiScenario) TemplateID(id int) int {
	return (id / len(s.ShiftTraffics)) / len(s.InitDowngradeFracs)
}
func (s *MultiScenario) DowngradeFracID(id int) int {
	return (id / len(s.ShiftTraffics)) % len(s.InitDowngradeFracs)
}

func (s *MultiScenario) ShiftID(id int) int { return id % len(s.ShiftTraffics) }

type FeedbackControlSummary struct {
	ItersToConverge  Stats `json:"itersToConverge"`
	NumDowngraded    Stats `json:"numDowngraded"`
	NumUpgraded      Stats `json:"numUpgraded"`
	NumOscillations  Stats `json:"numOscillations"`
	NumQoSChanged    Stats `json:"numQoSChanged"`
	NumRunsConverged int   `json:"numRunsConverged"`
}

type BasicDowngradeSummary struct {
	IntermediateOverage        Stats `json:"intermediateOverage"`
	IntermediateShortage       Stats `json:"intermediateShortage"`
	IntermediateOverOrShortage Stats `json:"intermediateOverOrShortage"`

	RealizedOverage        Stats `json:"realizedOverage"`
	RealizedShortage       Stats `json:"realizedShortage"`
	RealizedOverOrShortage Stats `json:"realizedOverOrShortage"`
}

type ScenarioResult struct {
	Scenario               FeedbackScenarioTemplate `json:"scenario"`
	InitDowngradeFrac      float64                  `json:"initDowngradeFrac"`
	ShiftTraffic           bool                     `json:"shiftTraffic"`
	NumDataPoints          int                      `json:"numDataPoints"`
	DowngradeSummary       *BasicDowngradeSummary   `json:"downgradeSummary"`
	FeedbackControlSummary *FeedbackControlSummary  `json:"feedbackControlSummary"`
}

type FeedbackInstanceResult struct {
	InstanceID                 ID             `json:"instanceID"`
	HostDemandsGen             string         `json:"hostDemandsGen"`
	NumHosts                   int            `json:"numHosts"`
	ApprovalOverExpectedDemand float64        `json:"approvalOverExpectedDemand"`
	NumSamplesAtApproval       int            `json:"numSamplesAtApproval"`
	NumFeedbackIters           int            `json:"numFeedbackIters"`
	Result                     ScenarioResult `json:"result"`
}
