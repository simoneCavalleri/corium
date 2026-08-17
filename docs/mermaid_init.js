/**
 * Mermaid.js Dynamic Renderer for Doxygen HTML Documentation
 * Converts Doxygen-rendered code fragments containing Mermaid diagrams into interactive SVGs.
 */

(function () {
    // 1. Dynamically load Mermaid.js ES module from CDN
    const script = document.createElement('script');
    script.type = 'module';
    script.src = 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
    
    script.onload = () => {
        // Render mermaid diagrams once DOM and script are ready
        renderMermaidDiagrams();
    };
    
    document.head.appendChild(script);

    function isMermaidSyntax(text) {
        const trimmed = text.trim();
        return (
            trimmed.startsWith('flowchart') ||
            trimmed.startsWith('graph') ||
            trimmed.startsWith('sequenceDiagram') ||
            trimmed.startsWith('classDiagram') ||
            trimmed.startsWith('stateDiagram') ||
            trimmed.startsWith('stateDiagram-v2') ||
            trimmed.startsWith('erDiagram') ||
            trimmed.startsWith('gantt') ||
            trimmed.startsWith('pie') ||
            trimmed.startsWith('gitGraph') ||
            trimmed.startsWith('C4Context') ||
            trimmed.startsWith('mindmap') ||
            trimmed.startsWith('timeline')
        );
    }

    async function renderMermaidDiagrams() {
        if (!window.mermaid) {
            try {
                const module = await import('https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs');
                window.mermaid = module.default;
            } catch (e) {
                console.warn('Could not load Mermaid.js module:', e);
                return;
            }
        }

        const isDarkMode = document.documentElement.classList.contains('dark-mode') || 
                           window.matchMedia('(prefers-color-scheme: dark)').matches;

        window.mermaid.initialize({
            startOnLoad: false,
            theme: isDarkMode ? 'dark' : 'default',
            securityLevel: 'loose',
            flowchart: {
                htmlLabels: true,
                useMaxWidth: true,
                curve: 'basis'
            }
        });

        // Query all potential Doxygen code elements
        const candidates = document.querySelectorAll(
            'div.fragment, pre.fragment, code.language-mermaid, pre.language-mermaid, div.line'
        );

        const processedBlocks = new Set();

        candidates.forEach((el) => {
            // Traverse up to find the outermost fragment container
            let container = el;
            if (el.classList.contains('line')) {
                container = el.closest('div.fragment') || el.closest('pre.fragment');
            }

            if (!container || processedBlocks.has(container)) {
                return;
            }

            const rawText = container.textContent || '';
            if (isMermaidSyntax(rawText)) {
                processedBlocks.add(container);

                const pre = document.createElement('pre');
                pre.className = 'mermaid';
                pre.style.background = 'transparent';
                pre.style.border = 'none';
                pre.style.textAlign = 'center';
                pre.style.margin = '1.5rem 0';
                pre.textContent = rawText.trim();

                container.parentNode.replaceChild(pre, container);
            }
        });

        try {
            await window.mermaid.run();
        } catch (err) {
            console.warn('Mermaid rendering failed for block:', err);
        }
    }

    // Run when DOM content is fully loaded
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', renderMermaidDiagrams);
    } else {
        renderMermaidDiagrams();
    }
})();
