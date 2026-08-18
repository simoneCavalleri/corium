/**
 * Mermaid.js Dynamic Renderer for Doxygen Documentation
 * Converts Doxygen-rendered code fragments containing Mermaid diagrams into interactive SVGs.
 */

(function () {
    function isMermaidSyntax(text) {
        if (!text) return false;
        const trimmed = text.trim();
        return /^(flowchart|graph|sequenceDiagram|classDiagram|stateDiagram|stateDiagram-v2|erDiagram|gantt|pie|gitGraph|C4Context|mindmap|timeline)\b/m.test(trimmed);
    }

    function extractCleanText(container) {
        const clone = container.cloneNode(true);
        // Remove line numbers if Doxygen added them
        clone.querySelectorAll('.lineno').forEach(el => el.remove());
        return clone.textContent.trim();
    }

    async function initMermaidRenderer() {
        if (typeof mermaid === 'undefined') {
            // Load fallback if not yet in head
            const script = document.createElement('script');
            script.src = 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js';
            script.onload = () => processAllMermaidBlocks();
            document.head.appendChild(script);
        } else {
            processAllMermaidBlocks();
        }
    }

    async function processAllMermaidBlocks() {
        if (typeof mermaid === 'undefined') return;

        const isDarkMode = document.documentElement.classList.contains('dark-mode') ||
                           window.matchMedia('(prefers-color-scheme: dark)').matches;

        mermaid.initialize({
            startOnLoad: false,
            theme: isDarkMode ? 'dark' : 'default',
            securityLevel: 'loose',
            flowchart: {
                htmlLabels: true,
                useMaxWidth: true,
                curve: 'basis'
            }
        });

        const candidates = document.querySelectorAll(
            'div.fragment, pre.fragment, code.language-mermaid, pre.language-mermaid'
        );

        const blocksToRender = [];

        candidates.forEach((container, index) => {
            const cleanText = extractCleanText(container);
            if (isMermaidSyntax(cleanText)) {
                const pre = document.createElement('pre');
                pre.className = 'mermaid';
                pre.id = `corium-mermaid-${index}`;
                pre.style.background = 'transparent';
                pre.style.border = 'none';
                pre.style.textAlign = 'center';
                pre.style.margin = '2rem 0';
                pre.textContent = cleanText;

                container.parentNode.replaceChild(pre, container);
                blocksToRender.push(pre);
            }
        });

        if (blocksToRender.length > 0) {
            try {
                await mermaid.run({
                    nodes: blocksToRender
                });
            } catch (err) {
                console.warn('Mermaid rendering warning:', err);
            }
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initMermaidRenderer);
    } else {
        initMermaidRenderer();
    }
})();
