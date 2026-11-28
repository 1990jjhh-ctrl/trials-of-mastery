Scriptname TrialsOfMasteryStatsSpike Hidden
{Spike S8 (docs/stats/PLAN.md): a throwaway script whose natives the Stats plugin binds.}

; Returns 42.
int Function SpikeAnswer() native global

; Writes msg to the Stats plugin's log.
Function SpikeLog(string msg) native global
