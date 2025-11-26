import React from "react";

interface LoaderProps {
    text?: string;
}

const Loader: React.FC<LoaderProps> = ({ text }) => (
    <div
        className="flex items-center justify-center min-h-[45dvh] w-full flex-col gap-4"
        aria-busy="true"
        aria-label="Loading"
    >
        <span className="animate-spin inline-block w-12 h-12 border-4 border-gray-200 border-t-teal-600 rounded-full" />
        {text && <span className="text-lg text-gray-500">{text}</span>}
    </div>
);

export default Loader;
