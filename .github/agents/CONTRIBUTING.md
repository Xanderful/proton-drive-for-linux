# Contributing to Chatmodes

Thank you for your interest in contributing to the Chatmodes collection! This document provides guidelines and instructions for contributing.

## 📋 Code of Conduct

- Be respectful and inclusive
- Provide constructive feedback
- Test your contributions before submitting
- Document your changes clearly

## 🤝 How to Contribute

### Adding a New Chatmode

1. **Create a new file** with the name format: `YourChatmodeName.chatmode.md`

2. **Follow the chatmode structure:**
   ```markdown
   # Your Chatmode Name
   
   ## 🎯 Purpose
   [Brief description of what this chatmode does]
   
   ## 👤 Personality & Perspective
   [How the AI should think and respond]
   
   ## 💪 Capabilities
   [What this chatmode excels at]
   
   ## 🚫 Constraints
   [Limitations and ethical boundaries]
   
   ## 📋 Output Format
   [How responses should be structured]
   
   ## 📚 Example Interactions
   [Usage patterns and scenarios]
   ```

3. **Test your chatmode:**
   - Use it in Claude, Copilot, or your preferred AI tool
   - Verify it produces the desired behavior
   - Refine based on results

4. **Submit a pull request:**
   - Include a clear description of the chatmode
   - Explain why it would be valuable to the collection
   - Provide any relevant context or use cases

### Improving Existing Chatmodes

1. **Identify the improvement:**
   - Better prompting
   - Additional capabilities
   - Clearer structure
   - Bug fixes

2. **Test thoroughly:**
   - Use the original and modified versions
   - Compare outputs
   - Ensure improvements work as intended

3. **Submit a pull request:**
   - Reference the chatmode being improved
   - Explain what changed and why
   - Provide before/after examples if applicable

### Updating Documentation

- Keep README.md current with new chatmodes
- Update USAGE_GUIDE.md with new patterns
- Fix typos and clarify unclear sections
- Add examples of effective chatmode usage

## 📝 Pull Request Process

1. **Fork the repository** (if not a direct contributor)

2. **Create a feature branch:**
   ```bash
   git checkout -b add/my-new-chatmode
   # or
   git checkout -b improve/existing-chatmode
   # or
   git checkout -b docs/update-usage-guide
   ```

3. **Make your changes** following the guidelines above

4. **Commit with clear messages:**
   ```bash
   git commit -m "Add: New chatmode for X use case"
   git commit -m "Improve: Enhanced Y chatmode with Z capability"
   git commit -m "Docs: Update README with new chatmode"
   ```

5. **Push to your branch:**
   ```bash
   git push origin add/my-new-chatmode
   ```

6. **Create a pull request** with:
   - Clear title describing the change
   - Description of what and why
   - Any relevant context or testing notes

## 🧪 Testing Chatmodes

Before submitting, test your chatmode:

### Quick Test
```bash
# Copy chatmode content
# Start a conversation with an AI tool
# Verify behavior matches intent
```

### Comprehensive Test
1. **Functional test:** Does it achieve its stated purpose?
2. **Edge case test:** How does it handle unusual requests?
3. **Integration test:** Does it play well with other chatmodes?
4. **Regression test:** Does it break existing workflows?

## 📊 Chatmode Quality Checklist

Before submitting, verify:

- [ ] Clear, descriptive title
- [ ] Well-defined purpose and goals
- [ ] Clear personality/perspective section
- [ ] Realistic capabilities listed
- [ ] Appropriate constraints documented
- [ ] Output format clearly specified
- [ ] Example interactions provided
- [ ] Tested in actual AI tool
- [ ] No personal/sensitive information
- [ ] Follows markdown formatting
- [ ] No formatting errors or typos

## 🏗️ Naming Conventions

- Use descriptive, clear names
- Use PascalCase for multi-word names: `MyNewChatmode.chatmode.md`
- Be specific about purpose: `frontend-winner` vs `frontend`
- Avoid generic names like `chatmode1.chatmode.md`

## 📦 File Structure Requirements

Each chatmode should be a single `.md` file in the root directory.

**Do:**
```
Architect.chatmode.md
4.1-Beast.chatmode.md
task-planner.chatmode.md
```

**Don't:**
```
chatmodes/Architect.md
Architect/chatmode.md
```

## 🔍 Review Process

1. **Automated checks:** Files are validated for structure and format
2. **Maintainer review:** Content is reviewed for quality and fit
3. **Community feedback:** Users may comment and suggest improvements
4. **Approval and merge:** Changes are merged when approved

## ❓ Questions or Discussions?

- **Issues:** Open an issue for problems or questions
- **Discussions:** Start a discussion for broader topics
- **Email:** Contact maintainers directly if preferred

## 🎓 Learning From Others

Review existing chatmodes to understand:
- Structure and formatting
- Best practices for prompting
- How to define capabilities clearly
- Effective constraint documentation

## 🚀 Getting Started

1. Choose a type of contribution (new chatmode, improvement, docs)
2. Review similar existing work
3. Create your contribution
4. Test thoroughly
5. Submit with clear documentation
6. Engage in review feedback

---

Thank you for contributing to making Chatmodes better for everyone! 🎉

**Questions?** Feel free to open an issue or discussion.

**Version:** 1.0  
**Last Updated:** October 31, 2025
