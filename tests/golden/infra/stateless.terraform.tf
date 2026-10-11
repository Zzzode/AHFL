terraform {
  required_providers {
    kubernetes = {
      source = "hashicorp/kubernetes"
      version = "~> 2.0"
    }

  }

}

provider "kubernetes" {
}

resource "kubernetes_manifest" "only" {
  manifest = {
    apiVersion = "ahfl.io/v1alpha1"
    kind = "Pinger"
    metadata = {
      name = "only"
    }

    spec = {
      state = "Idle"
    }

  }

}

