#!/usr/bin/env bash
# Run ON THE GITLAB SERVER (192.168.10.46) as root: sudo bash gitlab-diagnose.sh
# Explains why a push to devteam/hovernet main did not start a pipeline.
#   - shows the latest pipelines and any failure reason
#   - dry-runs pipeline creation for main (reports CI YAML errors exactly as GitLab sees them)
#   - lists runners and when they last contacted GitLab
# Set CREATE=1 to also start a pipeline for main if the dry run is clean.
set -Eeuo pipefail

PROJECT=${PROJECT:-devteam/hovernet}
REF=${REF:-main}
CREATE=${CREATE:-0}

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root: sudo bash $0" >&2
  exit 1
fi
command -v gitlab-rails >/dev/null || { echo "gitlab-rails not found (Omnibus install expected)" >&2; exit 2; }

gitlab-rails runner - <<RUBY
project = Project.find_by_full_path("${PROJECT}")
abort("Project ${PROJECT} not found") unless project
user = project.owner.is_a?(User) ? project.owner : User.admins.first
puts "== Project: #{project.full_path} (CI enabled: #{project.builds_enabled?})"

commit = project.repository.commit("${REF}")
puts "== ${REF} is at #{commit.sha[0, 7]}: #{commit.title}"

puts "\n== Latest pipelines"
pipelines = project.all_pipelines.order(id: :desc).limit(5)
pipelines.each do |p|
  puts "#{p.id} #{p.sha[0, 7]} #{p.ref} #{p.status} #{p.source} #{p.failure_reason}"
  puts "   yaml errors: #{p.yaml_errors}" if p.yaml_errors.present?
end
puts "(none)" if pipelines.empty?
puts "Pipeline for #{commit.sha[0, 7]}: " + (project.all_pipelines.where(sha: commit.sha).exists? ? "exists" : "MISSING -- push did not create one")

puts "\n== Dry-run pipeline creation for ${REF}"
result = Ci::CreatePipelineService.new(project, user, ref: "${REF}").execute(:push, dry_run: true)
pipeline = result.respond_to?(:payload) ? result.payload : result
errors = pipeline.respond_to?(:error_messages) ? pipeline.error_messages.map(&:content) : []
if errors.any? || pipeline.status == "failed"
  puts "CI CONFIG PROBLEM:"
  puts errors.presence || pipeline.yaml_errors
else
  puts "OK: #{pipeline.stages.count} stages, #{pipeline.builds.count} jobs would be created"
  puts pipeline.builds.map { |b| "  #{b.stage}: #{b.name}" }
end

puts "\n== Runners"
runners = (project.runners.to_a + Ci::Runner.instance_type.to_a).uniq
runners.each do |r|
  puts "#{r.id} #{r.description.inspect} tags=#{r.tag_list.to_a.join(',')} online=#{r.online?} contacted=#{r.contacted_at} paused=#{!r.active?}"
end
puts "(none)" if runners.empty?

if "${CREATE}" == "1" && errors.empty?
  puts "\n== Creating pipeline for ${REF}"
  res = Ci::CreatePipelineService.new(project, user, ref: "${REF}").execute(:web)
  p = res.respond_to?(:payload) ? res.payload : res
  puts "Created pipeline #{p.id} (#{p.status})"
end
RUBY
