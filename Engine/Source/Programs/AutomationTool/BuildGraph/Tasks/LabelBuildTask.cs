﻿using AutomationTool;
using EpicGames.MCP.Automation;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using UnrealBuildTool;

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters to label a given build in the MCP backend
	/// </summary>
	public class LabelBuildTaskParameters
	{
		/// <summary>
		/// The application name
		/// </summary>
		[TaskParameter]
		public string AppName;

        /// <summary>
        /// Unique build version of the app.
        /// </summary>
		[TaskParameter]
        public string BuildVersion;

        /// <summary>
        /// Platform we are posting info for.
        /// </summary>
		[TaskParameter]
        public MCPPlatform Platform;

		/// <summary>
		/// The label to apply to this build.
		/// </summary>
		[TaskParameter]
		public string Label;

		/// <summary>
		/// The MCP configuration name.
		/// </summary>
		[TaskParameter]
		public string McpConfig;
	}

	/// <summary>
	/// Implements a task which labels a given build in the MCP backend
	/// </summary>
	[TaskElement("LabelBuild", typeof(LabelBuildTaskParameters))]
	public class LabelBuildTask : CustomTask
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		LabelBuildTaskParameters Parameters;

		/// <summary>
		/// Construct a new LabelBuildTask.
		/// </summary>
		/// <param name="InParameters">Parameters for this task</param>
		public LabelBuildTask(LabelBuildTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		/// <returns>True if the task succeeded</returns>
		public override bool Execute(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			BuildPatchToolStagingInfo StagingInfo = new BuildPatchToolStagingInfo(Job.OwnerCommand, Parameters.AppName, 1, Parameters.BuildVersion, Parameters.Platform, null, null);
			string LabelWithPlatform = BuildInfoPublisherBase.Get().GetLabelWithPlatform(Parameters.Label, Parameters.Platform);
			BuildInfoPublisherBase.Get().LabelBuild(StagingInfo, LabelWithPlatform, Parameters.McpConfig);
			return true;
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter Writer)
		{
			Write(Writer, Parameters);
		}
	}
}
