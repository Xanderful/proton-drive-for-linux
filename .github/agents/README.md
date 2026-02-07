# 🤖 Chatmodes Collection

A curated collection of custom chatmode configurations for Claude AI and other AI assistants. These chatmodes define specialized behaviors, perspectives, and capabilities for different use cases.

## 📚 Available Chatmodes

### 🧠 Analytical & Problem-Solving

- **4.1-Beast.chatmode.md** - Advanced problem-solving with deep analysis, multi-perspective thinking, and comprehensive solutions
- **Thinking-Beast-Mode.chatmode.md** - Enhanced reasoning mode with sequential thinking and meta-cognitive analysis
- **Ultimate-Transparent-Thinking-Beast-Mode.chatmode.md** - Maximum transparency with quantum cognitive workflows and adversarial validation

### 🏗️ Architecture & Design

- **Architect.chatmode.md** - System design perspective with architecture focus, scalability analysis, and pattern recognition
- **task-planner.chatmode.md** - Project planning and task management with breakdown, estimation, and tracking

### 💻 Development & Frontend

- **frontend-winner.chatmode.md** - Specialized for frontend development with React, TypeScript, and modern web practices
- **groktalk.chatmode.md** - Deep technical understanding with explanatory capabilities
- **ProtonDriveCppGtk4.chatmode.md** - Specialized for Proton Drive Linux C/C++ (GTK4 + CMake), with safe, repo-specific workflows

### 🎯 Domain-Specific

- **prudent_trader_chatter.chatmode.md** - Specialized for Prudent Trader trading automation system
- **Sonnet4ever.chatmode.md** - Optimized Claude 3.5 Sonnet configuration

## 🚀 Quick Start

### Using Chatmodes Locally

1. **Clone this repository:**

   ```bash
   git clone https://github.com/Xanderful/chatmodes.git
   cd chatmodes
   ```

2. **In your IDE or AI tools, reference a chatmode:**

   - Copy the chatmode file path
   - Load it into your AI interface
   - The chatmode will shape the AI's behavior for your session

3. **Or link them in your project:**

   ```bash
   # In your project directory
   mkdir -p .github
   ln -s /path/to/chatmodes .github/chatmodes
   ```

## 📋 Chatmode Structure

Each chatmode file typically contains:

- **Objectives** - Clear goals and purposes
- **Personality/Perspective** - How the AI should think and respond
- **Capabilities** - What the AI should be good at
- **Constraints** - Limitations and ethical boundaries
- **Output Format** - How responses should be structured
- **Example Interactions** - Usage patterns and scenarios

## 🔧 How to Use

### In VS Code with GitHub Copilot

If your VS Code workspace has Copilot configured:

1. Create `.github/chatmodes` symlink:

   ```bash
   mkdir -p .github
   ln -s /path/to/this/repo/chatmodes .github/chatmodes
   ```

2. Reference in chat using Copilot's chat context

### In Claude Web Interface

1. Copy-paste the chatmode content at the start of a conversation
2. Claude will adapt to the specified mode
3. For best results, place chatmode content in the first message

### In Other AI Tools

1. Check if your tool supports system prompts or custom configurations
2. Load the chatmode content as a system prompt or configuration file
3. Start your conversation - the AI will follow the specified mode

## 🎓 Common Patterns

### Recommended Chatmode Combinations

**Deep Problem Solving:**

- Start with: Ultimate-Transparent-Thinking-Beast-Mode
- For complex, multi-faceted problems

**System Design:**

- Use: Architect
- For architecture decisions and scalability analysis

**Development Work:**

- Use: frontend-winner or task-planner
- For coding and planning tasks

**Trading System Development:**

- Use: prudent_trader_chatter
- For Prudent Trader specific development

## 📖 USAGE_GUIDE.md

For detailed examples and advanced usage patterns, see [USAGE_GUIDE.md](./USAGE_GUIDE.md)

## 🤝 Contributing

Contributions are welcome! See [CONTRIBUTING.md](./CONTRIBUTING.md) for guidelines on:

- Adding new chatmodes
- Improving existing ones
- Testing and validation
- Documentation updates

## 📄 License

This project is licensed under the MIT License - see [LICENSE](./LICENSE) file for details.

## 💡 Tips & Tricks

- **Combine chatmodes:** Mix and match elements from different chatmodes for custom configurations
- **Version control:** Keep these files in git to track improvements over time
- **Share:** Share chatmodes with your team for consistent AI interactions
- **Iterate:** Test chatmodes and refine them based on results
- **Feedback:** Document what works and what doesn't

## 🔗 Related Resources

- [VS Code Copilot Instructions](https://docs.github.com/en/copilot/customizing-copilot/adding-custom-instructions)
- [Claude Documentation](https://claude.ai/)
- [OpenAI System Prompts](https://platform.openai.com/docs/guides/system-prompts)

## 👤 Author

Created by Joseph (Xanderful)

## 📞 Support

For issues or questions:

1. Check existing chatmodes for similar patterns
2. Review USAGE_GUIDE.md for examples
3. Open an issue on GitHub
4. Check CONTRIBUTING.md for guidelines

---

**Last Updated:** October 31, 2025

Made with ❤️ for AI-assisted development
