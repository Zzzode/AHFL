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

resource "kubernetes_manifest" "primary" {
  manifest = {
    apiVersion = "ahfl.io/v1alpha1"
    kind = "Router"
    metadata = {
      name = "primary"
    }

    spec = {
      state = "Idle"
    }

  }

}

resource "kubernetes_manifest" "secondary" {
  manifest = {
    apiVersion = "ahfl.io/v1alpha1"
    kind = "Router"
    metadata = {
      name = "secondary"
    }

    spec = {
      state = "Idle"
    }

  }

  depends_on = [kubernetes_manifest.primary]
}

