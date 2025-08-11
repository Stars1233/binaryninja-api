use thiserror::Error;

/// A marker type for workflow registration errors
#[derive(Debug, Error)]
#[error("Failed to register workflow activity")]
pub struct WorkflowRegistrationError;

impl From<()> for WorkflowRegistrationError {
    fn from(_: ()) -> Self {
        WorkflowRegistrationError
    }
}

#[derive(Error, Debug)]
pub enum Error {
    #[error("Unable to retrieve low-level IL for function at {func_start:#x}")]
    MissingLowLevelIL { func_start: u64 },

    #[error("Unable to retrieve low-level SSA IL for function at {func_start:#x}")]
    MissingSsaForm { func_start: u64 },

    #[error("Unexpected LLIL operation at address {address:#x} (expected {expected})")]
    UnexpectedLlilOperation { address: u64, expected: String },

    #[error("Invalid selector at address {address:#x}")]
    InvalidSelector { address: u64 },

    #[error(transparent)]
    WorkflowRegistrationFailed(#[from] WorkflowRegistrationError),
}
