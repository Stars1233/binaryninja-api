use binaryninja::workflow::{activity, Activity, AnalysisContext, Workflow};

use crate::{activities, error::WorkflowRegistrationError};

const WORKFLOW_INFO: &str = r#"{
  "title": "Objective-C",
  "description": "Enhanced analysis for Objective-C code.",
  "capabilities": []
}"#;

fn run<E: std::fmt::Debug>(
    func: impl Fn(&AnalysisContext) -> Result<(), E>,
) -> impl Fn(&AnalysisContext) {
    move |ac| {
        if let Err(err) = func(ac) {
            log::debug!("Error occurred while running activity: {err:#x?}");
        }
    }
}

pub fn register_activities() -> Result<(), WorkflowRegistrationError> {
    let workflow =
        Workflow::cloned("core.function.metaAnalysis").ok_or(WorkflowRegistrationError)?;

    let objc_msg_send_calls_activity = Activity::new_with_action(
        activity::Config::action(
            "core.function.objectiveC.analyzeMessageSends",
            "Obj-C: Analyze Message Sends",
            "Analyze inline objc_msgSend calls, including applying call type adjustments and resolving to direct calls (if enabled)",
        ).eligibility(
            activity::Eligibility::auto().predicate(
                activity::ViewType::in_(["Mach-O", "DSCView"]),
        )),
        run(activities::objc_msg_send_calls::process),
    );

    let inline_stubs_activity = Activity::new_with_action(
        activity::Config::action(
            "core.function.objectiveC.inlineStubs",
            "Obj-C: Inline Message Send Stubs",
            "Inline Objective-C selector stubs, such as _objc_msgSend$foo, into their callers",
        )
        .eligibility(
            activity::Eligibility::without_setting()
                // The shared cache view does its own inlining of stub functions.
                .predicate(activity::ViewType::in_(["Mach-O"])),
        ),
        run(activities::inline_stubs::process),
    );

    workflow
        .activity_after(&inline_stubs_activity, "core.function.translateTailCalls")?
        .activity_after(&objc_msg_send_calls_activity, &inline_stubs_activity.name())?
        .register_with_config(WORKFLOW_INFO)?;

    Ok(())
}
