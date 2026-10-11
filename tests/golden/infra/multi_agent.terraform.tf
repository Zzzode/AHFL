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
    kind = "ReviewerA"
    metadata = {
      name = "first"
    }

    spec = {
      state = "Idle"
    }

  }

}

resource "kubernetes_manifest" "second" {
  manifest = {
    apiVersion = "ahfl.io/v1alpha1"
    kind = "ReviewerB"
    metadata = {
      name = "second"
    }

    spec = {
      state = "Idle"
    }

  }

  depends_on = [kubernetes_manifest.first]
}

