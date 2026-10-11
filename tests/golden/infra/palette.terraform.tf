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

resource "kubernetes_manifest" "first" {
  manifest = {
    apiVersion = "ahfl.io/v1alpha1"
    kind = "Painter"
    metadata = {
      name = "first"
    }

    spec = {
      state = "Idle"
    }

  }

}

