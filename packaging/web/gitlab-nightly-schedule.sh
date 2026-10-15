#!/usr/bin/env bash
# Run ON THE GITLAB SERVER as root: sudo bash gitlab-nightly-schedule.sh
# Creates (or updates) the nightly pipeline schedule that runs only the sanitizer
# job (build:game:linux:sanitizers). Idempotent. Override with PROJECT, CRON, TZ_NAME.
set -Eeuo pipefail

PROJECT=${PROJECT:-devteam/hovernet}
CRON=${CRON:-0 2 * * *}
TZ_NAME=${TZ_NAME:-Europe/London}

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root: sudo bash $0" >&2
  exit 1
fi

gitlab-rails runner - <<RUBY
project = Project.find_by_full_path("${PROJECT}") or abort("Project ${PROJECT} not found")
owner = project.owner.is_a?(User) ? project.owner : User.admins.first
schedule = project.pipeline_schedules.find_or_initialize_by(description: "Nightly sanitizers")
schedule.assign_attributes(ref: "refs/heads/#{project.default_branch}", cron: "${CRON}",
                           cron_timezone: "${TZ_NAME}", active: true)
schedule.owner = owner if schedule.owner.nil?
schedule.save!
puts "Schedule ##{schedule.id} '#{schedule.description}' on #{schedule.ref}: #{schedule.cron} (#{schedule.cron_timezone}), next run #{schedule.next_run_at}"
RUBY
