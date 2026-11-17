# The component layers (docs/COMPONENTS.md), one list of file stems per layer: the single source for BOTH the include check (CheckLayers.cmake) and
# the build (CMakeLists.txt makes one static library per list). A stem is a `src/<stem>.cpp`, an `include/sub0squad/<stem>.hpp`, or both.
# `platform` is the only layer that talks to the operating system (docs/PLATFORM.md); it uses nothing else of ours, which its link proof checks.
set(SUB0SQUAD_LAYER_NAMES platform kernel runtime domain harnesses agents orchestration presentation)
set(LAYER0 platform_error platform_process platform_proc platform_host platform_file platform_net platform_ipc platform_wait)
set(LAYER1 build_id paths frame journal job_spec job_store progress event_channel host_guard harness)
set(LAYER2 shim supervisor status mock_worker)
set(LAYER3 attempt_failure clarify stance engine plan division policy predicate mends contract scenario_compare process plan_lint plan_checks ledger workflow workflow_status overseer briefing recall trace triage edits outline canvas doc_layers turn_control throughput qa)
set(LAYER4 harness_registry harness_common harness_web harness_cpp cpp_templates harness_py py_templates calculator_spec web_templates sandbox)
set(LAYER5 llm_agent agent_card canvas_agent fork architect planner optimizer replay scenario blocks)
set(LAYER6 daemon daemon_impl daemon_jobs daemon_workflow daemon_governance)
set(LAYER7 portal portal_impl portal_sampler portal_files portal_page client cli cli_impl cli_jobs cli_project cli_tools cmd_process cmd_archive first_use wizard sub0squad)
