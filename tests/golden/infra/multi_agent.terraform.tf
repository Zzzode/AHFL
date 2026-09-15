resource "ahfl_workflow_node" "first" {
  target = "infra::multi_agent::ReviewerA"
}

resource "ahfl_workflow_node" "second" {
  target = "infra::multi_agent::ReviewerB"
  depends_on = [ahfl_workflow_node.first]
}

