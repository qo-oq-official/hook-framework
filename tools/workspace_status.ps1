$gitHash = (git rev-parse HEAD).Trim()
Write-Output "STABLE_GIT_COMMIT_HASH $gitHash"
