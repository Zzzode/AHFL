resource "ahfl_workflow_node" "primary" {
  target = "infra::service_mesh::Router"
}

resource "ahfl_workflow_node" "secondary" {
  target = "infra::service_mesh::Router"
}

