set(LLVM_LINK_COMPONENTS
  support
  FrontendOpenMP
  )

include(FetchContent)

FetchContent_Declare(
  quickjs
  GIT_REPOSITORY https://github.com/bellard/quickjs.git
  GIT_TAG        master 
)

FetchContent_MakeAvailable(quickjs)

add_library(libquickjs STATIC
    ${quickjs_SOURCE_DIR}/quickjs.c
    ${quickjs_SOURCE_DIR}/libregexp.c
    ${quickjs_SOURCE_DIR}/libunicode.c
    ${quickjs_SOURCE_DIR}/cutils.c
    ${quickjs_SOURCE_DIR}/dtoa.c
)

target_compile_definitions(libquickjs PRIVATE
    _GNU_SOURCE
    CONFIG_BIGNUM
    "CONFIG_VERSION=\"2024-01-13\""
)

target_include_directories(libquickjs PUBLIC 
    ${quickjs_SOURCE_DIR}
)

add_clang_library(clangTidyBugproneModule STATIC
  ArgumentCommentCheck.cpp
  AssertSideEffectCheck.cpp
  AssignmentInIfConditionCheck.cpp
  BadSignalToKillThreadCheck.cpp
  BitwisePointerCastCheck.cpp
  BoolPointerImplicitConversionCheck.cpp
  BranchCloneCheck.cpp
  BugproneTidyModule.cpp
  CapturingThisInMemberVariableCheck.cpp
  CastingThroughVoidCheck.cpp
  ChainedComparisonCheck.cpp
  ComparePointerToMemberVirtualFunctionCheck.cpp
  CopyConstructorInitCheck.cpp
  CrtpConstructorAccessibilityCheck.cpp
  DanglingHandleCheck.cpp
  DynamicStaticInitializersCheck.cpp
  EasilySwappableParametersCheck.cpp
  EmptyCatchCheck.cpp
  ExceptionEscapeCheck.cpp
  FoldInitTypeCheck.cpp
  ForwardDeclarationNamespaceCheck.cpp
  ForwardingReferenceOverloadCheck.cpp
  ImplicitWideningOfMultiplicationResultCheck.cpp
  InaccurateEraseCheck.cpp
  IncorrectEnableIfCheck.cpp
  IncorrectEnableSharedFromThisCheck.cpp
  InvalidEnumDefaultInitializationCheck.cpp
  UnintendedCharOstreamOutputCheck.cpp
  ReturnConstRefFromParameterCheck.cpp
  SuspiciousStringviewDataUsageCheck.cpp
  RegexCheck.cpp
  SwitchMissingDefaultCaseCheck.cpp
  IncDecInConditionsCheck.cpp
  IncorrectEnableIfCheck.cpp
  IncorrectRoundingsCheck.cpp
  InfiniteLoopCheck.cpp
  IntegerDivisionCheck.cpp
  LambdaFunctionNameCheck.cpp
  MacroParenthesesCheck.cpp
  MacroRepeatedSideEffectsCheck.cpp
  MisleadingSetterOfReferenceCheck.cpp
  MisplacedOperatorInStrlenInAllocCheck.cpp
  MisplacedPointerArithmeticInAllocCheck.cpp
  MisplacedWideningCastCheck.cpp
  MoveForwardingReferenceCheck.cpp
  MultiLevelImplicitPointerConversionCheck.cpp
  MultipleNewInOneExpressionCheck.cpp
  MultipleStatementMacroCheck.cpp
  NarrowingConversionsCheck.cpp
  NoEscapeCheck.cpp
  NonZeroEnumToBoolConversionCheck.cpp
  NondeterministicPointerIterationOrderCheck.cpp
  NotNullTerminatedResultCheck.cpp
  OptionalValueConversionCheck.cpp
  ParentVirtualCallCheck.cpp
  PointerArithmeticOnPolymorphicObjectCheck.cpp
  PosixReturnCheck.cpp
  RedundantBranchConditionCheck.cpp
  ReservedIdentifierCheck.cpp
  ReturnConstRefFromParameterCheck.cpp
  SharedPtrArrayMismatchCheck.cpp
  SignalHandlerCheck.cpp
  SignedCharMisuseCheck.cpp
  SizeofContainerCheck.cpp
  SizeofExpressionCheck.cpp
  SmartPtrArrayMismatchCheck.cpp
  SpuriouslyWakeUpFunctionsCheck.cpp
  StandaloneEmptyCheck.cpp
  StringConstructorCheck.cpp
  StringIntegerAssignmentCheck.cpp
  StringLiteralWithEmbeddedNulCheck.cpp
  StringviewNullptrCheck.cpp
  SuspiciousEnumUsageCheck.cpp
  SuspiciousIncludeCheck.cpp
  SuspiciousMemoryComparisonCheck.cpp
  SuspiciousMemsetUsageCheck.cpp
  SuspiciousMissingCommaCheck.cpp
  SuspiciousReallocUsageCheck.cpp
  SuspiciousSemicolonCheck.cpp
  SuspiciousStringCompareCheck.cpp
  SuspiciousStringviewDataUsageCheck.cpp
  SwappedArgumentsCheck.cpp
  SwitchMissingDefaultCaseCheck.cpp
  TaggedUnionMemberCountCheck.cpp
  TerminatingContinueCheck.cpp
  ThrowKeywordMissingCheck.cpp
  TooSmallLoopVariableCheck.cpp
  UncheckedOptionalAccessCheck.cpp
  UndefinedMemoryManipulationCheck.cpp
  UndelegatedConstructorCheck.cpp
  UnhandledExceptionAtNewCheck.cpp
  UnhandledSelfAssignmentCheck.cpp
  UniquePtrArrayMismatchCheck.cpp
  UnsafeFunctionsCheck.cpp
  UnusedLocalNonTrivialVariableCheck.cpp
  UnusedRaiiCheck.cpp
  UnusedReturnValueCheck.cpp
  UseAfterMoveCheck.cpp
  VirtualNearMissCheck.cpp

  LINK_LIBS
  clangTidy
  clangTidyUtils

  DEPENDS
  omp_gen
  ClangDriverOptions
  )

clang_target_link_libraries(clangTidyBugproneModule
  PRIVATE
  clangAnalysis
  clangAnalysisFlowSensitive
  clangAnalysisFlowSensitiveModels
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  clangSema
  clangTooling
  clangTransformer
  libquickjs
  m
  dl
  pthread
  )
